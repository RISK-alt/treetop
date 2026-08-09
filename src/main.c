#include "treetop.h"
#include "app.h"
#include "render.h"
#include "console.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
                            unsigned int refresh_ms)
{
    unsigned long long  now;
    unsigned long long  remaining_ms;

    now = plat_now();
    if (now >= next_sample_at)
        return (0);
    remaining_ms = (next_sample_at - now) / MAIN_TICKS_PER_MS;
    if (remaining_ms > (unsigned long long)refresh_ms)
        remaining_ms = refresh_ms;
    return ((unsigned int)remaining_ms);
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
** never accumulates it tick over tick. If a sample runs long enough to
** push next_sample_at into the past, wait_timeout_ms() reports a 0 ms
** wait for the next iteration - the loop catches up immediately rather
** than blocking past a deadline it already missed or busy-spinning (a
** 0 ms wait still blocks in con_wait_key until a key or timeout, exactly
** once, not in a tight loop).
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
        key = con_wait_key(wait_timeout_ms(next_sample_at, a.refresh_ms));
        if (key != 0)
        {
            if (key == TT_KEY_RESIZE)
                con_size(&cols, &rows);
            redraw |= keys_handle(&a, key);
        }
        if (plat_now() >= next_sample_at && !a.paused)
        {
            was_first = a.first_tick;
            app_sample(&a);
            if (was_first)
                select_first_row(&a);
            next_sample_at += (unsigned long long)a.refresh_ms
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
