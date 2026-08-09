#include "treetop.h"
#include "app.h"
#include "render.h"
#include "console.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/*
** plat_now() reports Windows FILETIME ticks (100 ns each), not
** milliseconds - see app.c's own APP_PORT_INTERVAL_TICKS for the same
** conversion. The event loop's whole schedule (next_sample_at,
** con_wait_key's timeout) is kept in these native ticks and only
** converted to milliseconds at the one place that needs it:
** con_wait_key's argument.
*/
#define MAIN_TICKS_PER_MS   10000ULL

/*
** render_all() never invents a selection on its own - see its comment in
** chrome.c - so main() is the one place responsible for pointing
** a->selected at a real row before the first frame the user should see
** something highlighted in. Later ticks need no equivalent call: a
** surviving selection carries forward by key through app_sample()
** (app.c step 10), and a selection that dies is recovered by
** keys_handle()'s own move_selection() the next time an arrow key is
** pressed - main() does not have to babysit it continuously.
*/
static void select_first_row(t_app *a)
{
    t_process   **rows;
    size_t      n;

    if (a->cur.count == 0)
        return ;
    rows = malloc(sizeof(t_process *) * a->cur.count);
    if (rows == NULL)
        return ;
    n = view_flatten(&a->cur, &a->view, rows, a->cur.count);
    if (n > 0)
        a->selected = rows[0]->key;
    free(rows);
}

/*
** While paused, con_wait_key() must NOT be timed against next_sample_at
** at all: app_sample() is skipped while paused (see run_interactive()),
** so next_sample_at sits frozen at whatever it was when 'p' was pressed.
** Real time keeps moving, and within at most one refresh_ms it passes
** next_sample_at - after that this would return 0 on every call forever,
** and con_wait_key(0) returns immediately, so the loop would spin at
** 100% of a core for as long as the tool stayed paused. That is the
** worst possible failure for a tool whose entire purpose is showing
** people what is eating their CPU, and it produces no visible symptom
** (no redraw is even triggered), which is exactly why it must be fixed
** here rather than relied on to show up in testing. MAIN_PAUSED_POLL_MS
** gives the loop a fixed, harmless poll interval instead - long enough
** to cost nothing, short enough that unpausing still feels immediate.
*/
#define MAIN_PAUSED_POLL_MS 250u

/*
** Clamps con_wait_key's timeout to [0, refresh_ms]: never negative (a
** sample that overran its own interval must not turn into a negative
** wait, which - cast to the unsigned milliseconds con_wait_key expects -
** would otherwise become a multi-billion-millisecond wait, i.e. the loop
** hangs on the very overrun it was supposed to recover from) and never
** above refresh_ms (so a stale next_sample_at computed before the user
** just lowered the interval with '-' cannot block past the new, shorter
** one - the next iteration re-derives the wait fresh from the current
** refresh_ms every single time, nothing is cached across ticks).
*/
static unsigned int    wait_timeout_ms(unsigned long long next_sample_at,
                            unsigned int refresh_ms, int paused)
{
    unsigned long long  now;
    unsigned long long  remaining_ms;

    if (paused)
        remaining_ms = MAIN_PAUSED_POLL_MS;
    else
    {
        now = plat_now();
        if (now >= next_sample_at)
            remaining_ms = 0;
        else
            remaining_ms = (next_sample_at - now) / MAIN_TICKS_PER_MS;
    }
    if (remaining_ms > (unsigned long long)refresh_ms)
        remaining_ms = refresh_ms;
    return ((unsigned int)remaining_ms);
}

/*
** Services a->confirm_go: keys_handle() (src/input/keys.c, treetop_core)
** raised it because the literal key 'y' was pressed while a kill
** confirmation was open, but it could not call plat_kill() itself - that
** symbol is declared in platform.h but only DEFINED in
** src/platform/win_kill.c, which links into the treetop executable, not
** treetop_core (treetop_tests never links it, which is also exactly why
** plat_kill() cannot be unit-tested - see the brief). This is therefore
** the one place in the whole codebase that actually calls it.
**
** a->confirm_victims is walked in the order it was collected -
** tree_collect_subtree() (src/model/tree.c) already produced it
** deepest-first for a subtree kill, so children are terminated before
** the parent that might be waiting on them; a single F9 kill has exactly
** one entry and order does not matter. Each victim is passed to
** plat_kill() by KEY, not by any pointer or row this file holds - the
** re-verification against a live handle's own creation time happens
** entirely inside plat_kill(), immediately before TerminateProcess. This
** function does not, and must not, attempt that check itself.
**
** Every victim is attempted even after an earlier one fails - a subtree
** kill where one process is protected should still terminate the rest,
** not abandon the whole operation on the first denial. denied takes
** priority over gone in the footer message: "access denied" is
** actionable (re-run elevated), "already exited" is not, so if both
** happened this tick the actionable one is what the user needs to see.
** Full success clears kill_status back to empty, which is what makes
** draw_footer() (src/render/chrome.c) fall through to the ordinary key
** bar again.
*/
static void execute_confirmed_kill(t_app *a)
{
    size_t  i;
    int     rc;
    int     denied;
    int     gone;

    denied = 0;
    gone = 0;
    i = 0;
    while (i < a->confirm_count)
    {
        rc = plat_kill(a->confirm_victims[i]);
        if (rc == -1)
            denied = 1;
        else if (rc == -2)
            gone = 1;
        i++;
    }
    if (denied)
        wcsncpy(a->kill_status,
                L"access denied - try running as administrator", 63);
    else if (gone)
        wcsncpy(a->kill_status, L"process already exited", 63);
    else
        a->kill_status[0] = L'\0';
    a->kill_status[63] = L'\0';
    free(a->confirm_victims);
    a->confirm_victims = NULL;
    a->confirm_count = 0;
    a->confirm_open = 0;
    a->confirm_subtree = 0;
    a->confirm_go = 0;
}

/*
** The interactive event loop - the only function in this codebase
** allowed to call con_*, per the brief. Everything it does beyond that
** is delegate: keys_handle() (src/input/keys.c, treetop_core) owns every
** binding's actual effect, app_sample() owns collecting a fresh table,
** render_all() owns drawing it. This function's only real content is the
** scheduling: waiting on input with a timeout equal to the time left
** until the next sample is what keeps keys instant at any --refresh
** value without the sampling cadence drifting - a fixed Sleep() would
** make 'q' feel broken at, say, --refresh 5000.
**
** next_sample_at is advanced by += refresh_ms rather than reset to
** "now + refresh_ms": a sample that itself takes noticeable time (a busy
** machine, a slow plat_processes() call) then loses only the overrun,
** never accumulates it tick over tick. But that additive step alone
** only ever recovers one refresh_ms of backlog per iteration - after
** the loop was paused for a while (next_sample_at frozen, see
** wait_timeout_ms()) or the machine slept and resumed, next_sample_at
** can sit real hours behind "now", and without a limit this would
** replay that entire backlog as a tight burst of back-to-back
** app_sample() calls until it caught up. The snap-forward below (if
** still behind after the one += step, jump straight to now + refresh_ms
** instead of looping) is what turns that burst into a single sample.
*/
static int  run_interactive(void)
{
    t_app                a;
    t_frame              f;
    int                  cols;
    int                  rows;
    int                  key;
    int                  redraw;
    int                  was_first;
    unsigned long long   now;
    unsigned long long   next_sample_at;

    if (app_init(&a) != 0)
    {
        tt_warn("failed to initialise");
        return (1);
    }
    if (con_init() != 0)
    {
        tt_warn("failed to initialise the console");
        app_free(&a);
        return (1);
    }
    if (frame_init(&f, 8192) != 0)
    {
        tt_warn("out of memory");
        con_shutdown();
        app_free(&a);
        return (1);
    }
    con_size(&cols, &rows);
    /* Due immediately: the first loop iteration samples right away. */
    next_sample_at = plat_now();
    redraw = 0;
    while (a.running)
    {
        key = con_wait_key(wait_timeout_ms(next_sample_at, a.refresh_ms,
                    a.paused));
        if (key != 0)
        {
            if (key == TT_KEY_RESIZE)
                con_size(&cols, &rows);
            redraw |= keys_handle(&a, key);
            if (a.confirm_go)
            {
                execute_confirmed_kill(&a);
                redraw = 1;
            }
        }
        now = plat_now();
        if (now >= next_sample_at && !a.paused)
        {
            was_first = a.first_tick;
            app_sample(&a);
            if (was_first)
                select_first_row(&a);
            next_sample_at += (unsigned long long)a.refresh_ms
                    * MAIN_TICKS_PER_MS;
            now = plat_now();
            if (now >= next_sample_at)
                next_sample_at = now + (unsigned long long)a.refresh_ms
                        * MAIN_TICKS_PER_MS;
            redraw = 1;
        }
        if (redraw)
        {
            render_all(&f, &a, cols, rows, a.limited);
            con_write(f.buf, f.len);
            redraw = 0;
        }
    }
    frame_free(&f);
    con_shutdown();
    app_free(&a);
    return (0);
}

/*
** --json samples twice with the refresh interval in between: cpu_pct is
** a delta between two samples, so a single sample would report zero for
** every process and the whole point of this flag - letting an agent see
** its own real footprint - would be a lie.
*/
static int  run_json(void)
{
    t_app   a;

    if (app_init(&a) != 0)
    {
        tt_warn("failed to initialise");
        return (1);
    }
    if (a.limited)
        tt_warn("running in limited mode (ntdll unavailable)");
    app_sample(&a);
    Sleep(a.refresh_ms);
    app_sample(&a);
    json_emit(&a.cur, &a.sys, stdout);
    app_free(&a);
    return (0);
}

int     main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--selftest") == 0)
        return (app_selftest());
    if (argc > 1 && strcmp(argv[1], "--json") == 0)
        return (run_json());
    return (run_interactive());
}
