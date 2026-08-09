#pragma once

/*                                  INCLUDES                                  */

# include "platform.h"
# include "process.h"
# include "input.h"
# include "agent.h"

/*                                   APP                                      */

/*
** Owns both live tables and everything the sampling loop - and, once it
** exists, the renderer - needs across ticks.
**
** cur/prev are a ping-pong pair: app_sample() swaps which struct holds
** which heap array every tick rather than copying, so there is always
** exactly one owner for each allocation. selected is stored as a key,
** not a row index or a t_process pointer, because row order and row
** addresses both change on every refresh (table_add can realloc) while
** the (pid, create_time) identity does not.
**
** first_tick and last_port_ms are private bookkeeping for app_sample():
** the former tells delta_apply() there is no previous sample yet, the
** latter drives the port refresh's own 3-second wall-clock timer, which
** must stay independent of refresh_ms.
**
** paused, filter_mode and help_open are Task 19's additions for the
** event loop: paused stops app_sample() from being called on schedule
** without freezing the loop itself (keys must stay responsive at any
** time); filter_mode is what makes an ordinary keystroke like 'p' type
** into the filter instead of pausing the display while '/' is active;
** help_open gates the full-screen binding list. All three are read and
** written only by keys_handle() (src/input/keys.c) and src/main.c - no
** other code in this codebase has a reason to touch them.
**
** Task 20's kill confirmation is the same shape, split across two files
** for a reason: keys_handle() (treetop_core, no Win32) decides WHAT to
** kill and owns opening/cancelling the dialog, but it cannot call
** plat_kill() itself - that symbol only links into the treetop
** executable, not treetop_tests. confirm_open/confirm_subtree/
** confirm_count/confirm_victims are set by keys_handle() when F9 or
** Shift+F9 fires on a real selection; confirm_go is the one-shot signal
** it raises on 'y' for src/main.c to notice, actually call plat_kill()
** for each victim, and clear every one of these fields back to "no
** dialog open" afterwards. confirm_victims holds KEYS, not t_process
** pointers or a snapshot of the row list - table_add() can realloc cur's
** backing array on the very next sample while the dialog sits open, and
** a (pid, create_time) key is the only thing in this codebase that
** survives that. render_all() (src/render/chrome.c) re-resolves each key
** against the live a->cur every frame to decide what draw_confirm()
** actually shows, so a victim that exits while the dialog is still open
** simply drops out of the displayed list rather than dangling.
** kill_status carries the footer's failure text ("access denied...",
** "process already exited") after src/main.c calls plat_kill(); it is
** cleared whenever a fresh confirmation opens and otherwise persists
** until the next kill attempt overwrites it - there is no separate timer
** for it, matching how a->paused persists until explicitly toggled.
*/
typedef struct s_app
{
    t_table             cur;
    t_table             prev;
    t_sysinfo           sys;
    t_view              view;
    int                 running;
    unsigned int        refresh_ms;
    int                 limited;
    t_proc_key          selected;

    int                 first_tick;
    unsigned long long  last_port_ms;

    int                 paused;
    int                 filter_mode;
    int                 help_open;

    int                 confirm_open;
    int                 confirm_subtree;
    int                 confirm_go;
    t_proc_key          *confirm_victims;
    size_t              confirm_count;
    wchar_t             kill_status[64];
}   t_app;

int         app_init(t_app *a);
void        app_sample(t_app *a);
void        app_free(t_app *a);
int         app_selftest(void);
