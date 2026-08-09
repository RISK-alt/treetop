#include "app.h"
#include "console.h"

#include <stdlib.h>
#include <wchar.h>

/*
** treetop's interaction layer: keys_handle() is the single entry point
** the event loop (src/main.c) calls for every keystroke con_wait_key()
** reports, and the only place in the codebase - besides app_sample()
** carrying a->selected/collapsed forward across a resample - that
** mutates a->selected, a->view, a->paused, a->filter_mode or
** a->help_open. It touches no Win32 API, only the TT_KEY_* constants
** from console.h, so it lives in treetop_core and is unit-testable
** without a live terminal (tests/test_keys.c).
**
** Precedence, checked in this order every call: TT_KEY_RESIZE always
** forces a redraw and is otherwise a no-op here (re-reading con_size is
** a console operation - src/main.c's job, not this file's); filter mode,
** when active, swallows every ordinary binding below; the help overlay,
** when open, treats ANY key as "dismiss" per its own "press any key to
** close" hint (chrome.c); only then do the ordinary bindings apply.
*/

#define TT_REFRESH_STEP_MS  250u
#define TT_REFRESH_MIN_MS   100u
#define TT_REFRESH_MAX_MS   60000u

/*                                SELECTION                                   */

/*
** Selection is a (pid, create_time) key, never a row index or pointer:
** row order changes every tick (sort, filter, tree/flat, even the OS's
** own enumeration order) and a t_process* can be invalidated by the next
** table_add() realloc. Moving it therefore means: flatten the CURRENT
** view fresh, locate the row that carries the current key, and step by
** one row in that freshly-flattened order - never by adjusting a
** remembered integer.
**
** When the key is not found at all - nothing selected yet (a freshly
** zeroed t_app), the previously selected process has since exited, or
** the active filter no longer matches it - there is no "current row" to
** step from, so this recovers to the first visible row regardless of
** direction. That is a real change (a->selected moves) and is reported
** as one; stepping past either end of an already-tracked selection is
** not (the key does not move) and is reported as a no-op, matching
** keys_handle's "1 only if something actually changed" contract.
*/
static int  move_selection(t_app *a, int dir)
{
    t_process   **rows;
    size_t      n;
    size_t      i;
    size_t      idx;
    size_t      next;
    int         found;

    if (a->cur.count == 0)
        return (0);
    rows = malloc(sizeof(t_process *) * a->cur.count);
    if (rows == NULL)
        return (0);
    n = view_flatten(&a->cur, &a->view, rows, a->cur.count);
    if (n == 0)
    {
        free(rows);
        return (0);
    }
    found = 0;
    idx = 0;
    i = 0;
    while (i < n)
    {
        if (key_eq(rows[i]->key, a->selected))
        {
            idx = i;
            found = 1;
            break ;
        }
        i++;
    }
    if (!found)
    {
        a->selected = rows[0]->key;
        free(rows);
        return (1);
    }
    next = idx;
    if (dir < 0 && idx > 0)
        next = idx - 1;
    else if (dir > 0 && idx + 1 < n)
        next = idx + 1;
    if (next == idx)
    {
        free(rows);
        return (0);
    }
    a->selected = rows[next]->key;
    free(rows);
    return (1);
}

/*                                COLLAPSE                                    */

static int  set_collapsed(t_app *a, int want_collapsed)
{
    t_process   *p;

    p = table_find(&a->cur, a->selected);
    if (p == NULL || p->collapsed == want_collapsed)
        return (0);
    p->collapsed = want_collapsed;
    return (1);
}

static int  toggle_collapsed(t_app *a)
{
    t_process   *p;

    p = table_find(&a->cur, a->selected);
    if (p == NULL)
        return (0);
    p->collapsed = !p->collapsed;
    return (1);
}

/*                                   SORT                                     */

/* Cycles t_sort_mode's four values (PID, CPU, MEM, NAME) in enum order. */
static int  cycle_sort(t_app *a, int dir)
{
    int mode;

    mode = ((int)a->view.sort + dir + 4) % 4;
    a->view.sort = (t_sort_mode)mode;
    return (1);
}

/*                                 REFRESH                                    */

/*
** Signed 64-bit intermediate so neither direction can wrap: subtracting
** the step from a refresh_ms already near TT_REFRESH_MIN_MS must clamp
** at the floor, not wrap an unsigned int to a huge value (which would
** turn the event loop's wait timeout into an effective hang) or drop to
** zero (a zero timeout would busy-spin con_wait_key instead).
*/
static int  adjust_refresh(t_app *a, int dir)
{
    long long   next;

    next = (long long)a->refresh_ms + (long long)dir * TT_REFRESH_STEP_MS;
    if (next < (long long)TT_REFRESH_MIN_MS)
        next = TT_REFRESH_MIN_MS;
    if (next > (long long)TT_REFRESH_MAX_MS)
        next = TT_REFRESH_MAX_MS;
    if ((unsigned int)next == a->refresh_ms)
        return (0);
    a->refresh_ms = (unsigned int)next;
    return (1);
}

/*                                  FILTER                                    */

static int  clear_filter(t_app *a)
{
    if (a->view.filter[0] == L'\0')
        return (0);
    a->view.filter[0] = L'\0';
    return (1);
}

/*
** Escape/Enter/Backspace are handled explicitly; anything else in the
** printable Unicode range (0x20..0xffff - everything below TT_KEY_RESIZE's
** 0x10001 floor, see console.h) is ordinary text and gets appended.
** Every other symbolic key (arrows, F-keys, ...) falls through to the
** final `return (0)`: swallowed, exactly as the brief requires - typing
** while filtering must never trigger a binding.
**
** TT_FILTER_LEN counts the NUL terminator (treetop.h), so the last
** writable index is TT_FILTER_LEN - 2; the `len + 1 >= TT_FILTER_LEN`
** guard below is what stops the buffer from ever overflowing.
*/
static int  filter_key(t_app *a, int key)
{
    size_t  len;

    if (key == TT_KEY_ESCAPE)
    {
        a->view.filter[0] = L'\0';
        a->filter_mode = 0;
        return (1);
    }
    if (key == TT_KEY_ENTER)
    {
        a->filter_mode = 0;
        return (1);
    }
    if (key == TT_KEY_BACKSPACE)
    {
        len = wcslen(a->view.filter);
        if (len == 0)
            return (0);
        a->view.filter[len - 1] = L'\0';
        return (1);
    }
    if (key >= 0x20 && key < 0x10000)
    {
        len = wcslen(a->view.filter);
        if (len + 1 >= TT_FILTER_LEN)
            return (0);
        a->view.filter[len] = (wchar_t)key;
        a->view.filter[len + 1] = L'\0';
        return (1);
    }
    return (0);
}

/*                                  NORMAL                                    */

/*
** F9 / Shift+F9 (kill) are deliberately absent - Task 20's job, not this
** one's (see the brief). They fall through to the final `return (0)`
** along with every other unbound key.
*/
static int  normal_key(t_app *a, int key)
{
    if (key == TT_KEY_UP)
        return (move_selection(a, -1));
    if (key == TT_KEY_DOWN)
        return (move_selection(a, 1));
    if (key == TT_KEY_LEFT)
        return (set_collapsed(a, 1));
    if (key == TT_KEY_RIGHT)
        return (set_collapsed(a, 0));
    if (key == TT_KEY_SPACE)
        return (toggle_collapsed(a));
    if (key == (int)L'a')
    {
        a->view.agents_only = !a->view.agents_only;
        return (1);
    }
    if (key == (int)L'o')
    {
        a->view.orphans_only = !a->view.orphans_only;
        return (1);
    }
    if (key == (int)L'/')
    {
        a->filter_mode = 1;
        return (1);
    }
    if (key == TT_KEY_ESCAPE)
        return (clear_filter(a));
    if (key == TT_KEY_F5)
    {
        a->view.tree_mode = !a->view.tree_mode;
        return (1);
    }
    if (key == TT_KEY_F6 || key == (int)L'>')
        return (cycle_sort(a, 1));
    if (key == (int)L'<')
        return (cycle_sort(a, -1));
    if (key == (int)L'p')
    {
        a->paused = !a->paused;
        return (1);
    }
    if (key == (int)L'+')
        return (adjust_refresh(a, 1));
    if (key == (int)L'-')
        return (adjust_refresh(a, -1));
    if (key == TT_KEY_F1 || key == (int)L'?')
    {
        a->help_open = !a->help_open;
        return (1);
    }
    if (key == (int)L'q')
    {
        a->running = 0;
        return (1);
    }
    return (0);
}

/*                                  ENTRY                                     */

int     keys_handle(t_app *a, int key)
{
    if (key == TT_KEY_RESIZE)
        return (1);
    if (a->filter_mode)
        return (filter_key(a, key));
    if (a->help_open)
    {
        a->help_open = 0;
        return (1);
    }
    return (normal_key(a, key));
}
