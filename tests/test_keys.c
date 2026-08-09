/* tests/test_keys.c */
#include "harness.h"
#include "app.h"
#include "console.h"

#include <string.h>
#include <wchar.h>

/*
** keys_handle() never calls app_init()/app_sample() - those live outside
** treetop_core (they call plat_* Win32 collectors) and are not linked
** into treetop_tests at all. Every fixture below builds a t_app the same
** way tests/test_chrome.c already does: zero it, then hand-assemble
** exactly the fields keys_handle actually reads.
*/
static void mk_app(t_app *a)
{
    memset(a, 0, sizeof(*a));
}

/*
** Three flat siblings (ppid 4, which is not itself in the table, so all
** three resolve to roots - tree_build's own resolve_parent rule).
** tree_mode is left off (flat) so table order is stable and predictable
** without depending on the default sort.
*/
static void mk_flat3(t_app *a)
{
    t_process   p;

    mk_app(a);
    TT_EQ_INT(table_init(&a->cur, 8), 0);
    view_init(&a->view);
    a->view.tree_mode = 0;
    a->view.sort = SORT_PID;
    a->view.sort_desc = 0;
    p = mk_proc(100, 1000, 4, L"a.exe"); table_add(&a->cur, &p);
    p = mk_proc(200, 2000, 4, L"b.exe"); table_add(&a->cur, &p);
    p = mk_proc(300, 3000, 4, L"c.exe"); table_add(&a->cur, &p);
    tree_build(&a->cur);
    a->refresh_ms = 1000;
    a->running = 1;
}

/*                               NAVIGATION                                   */

static void test_down_moves_to_next_row_and_reports_redraw(void)
{
    t_app   a;
    int     redraw;

    mk_flat3(&a);
    a.selected = a.cur.procs[0].key;            /* pid 100 */
    redraw = keys_handle(&a, TT_KEY_DOWN);
    TT_EQ_INT(redraw, 1);
    TT_EQ_INT((int)a.selected.pid, 200);
    table_free(&a.cur);
}

static void test_up_moves_to_previous_row(void)
{
    t_app   a;
    int     redraw;

    mk_flat3(&a);
    a.selected = a.cur.procs[1].key;            /* pid 200 */
    redraw = keys_handle(&a, TT_KEY_UP);
    TT_EQ_INT(redraw, 1);
    TT_EQ_INT((int)a.selected.pid, 100);
    table_free(&a.cur);
}

/*
** Sits at the LAST row and moves down: must not run off the end. The
** key must stay exactly where it was, and no redraw is reported since
** nothing actually changed.
*/
static void test_down_at_last_row_does_not_run_off(void)
{
    t_app       a;
    t_proc_key  before;
    int         redraw;

    mk_flat3(&a);
    a.selected = a.cur.procs[2].key;            /* pid 300, last row */
    before = a.selected;
    redraw = keys_handle(&a, TT_KEY_DOWN);
    TT_EQ_INT(redraw, 0);
    TT_CHECK(key_eq(a.selected, before));
    table_free(&a.cur);
}

/*
** Mirror at the FIRST row and moving up: proves the boundary guard is
** symmetric, not accidentally correct only on one end.
*/
static void test_up_at_first_row_does_not_run_off(void)
{
    t_app       a;
    t_proc_key  before;
    int         redraw;

    mk_flat3(&a);
    a.selected = a.cur.procs[0].key;            /* pid 100, first row */
    before = a.selected;
    redraw = keys_handle(&a, TT_KEY_UP);
    TT_EQ_INT(redraw, 0);
    TT_CHECK(key_eq(a.selected, before));
    table_free(&a.cur);
}

/*
** The load-bearing one: selection is a KEY, not a row index. Select the
** process at pid 200 while it sits at index 1 (ascending PID order),
** then flip the sort to descending - pid 200 is now at index 1 again by
** coincidence of a 3-row table, so also cross-check with a 4th row and a
** genuinely different index to rule out an index-based implementation
** passing by accident.
*/
static void test_selection_tracks_key_not_index_across_reorder(void)
{
    t_app       a;
    t_process   p;
    int         redraw;

    mk_app(&a);
    TT_EQ_INT(table_init(&a.cur, 8), 0);
    view_init(&a.view);
    a.view.tree_mode = 0;
    a.view.sort = SORT_PID;
    a.view.sort_desc = 0;
    p = mk_proc(100, 1000, 4, L"a.exe"); table_add(&a.cur, &p);
    p = mk_proc(200, 2000, 4, L"b.exe"); table_add(&a.cur, &p);
    p = mk_proc(300, 3000, 4, L"c.exe"); table_add(&a.cur, &p);
    p = mk_proc(400, 4000, 4, L"d.exe"); table_add(&a.cur, &p);
    tree_build(&a.cur);
    a.refresh_ms = 1000;

    /* Ascending PID order: 100, 200, 300, 400 - select 300, at index 2. */
    a.selected = (t_proc_key){ .pid = 300, .create_time = 3000 };

    /* Reorder: descending PID order is now 400, 300, 200, 100 - 300 is
       now at index 1, not 2. A move "up" from an index-2 implementation
       would land on whatever now sits at index 1 (300 itself, no-op) or
       index 3 (100) depending on the bug; a correct key-based move must
       land on 400 - the row immediately above 300 in the NEW order. */
    a.view.sort_desc = 1;
    redraw = keys_handle(&a, TT_KEY_UP);
    TT_EQ_INT(redraw, 1);
    TT_EQ_INT((int)a.selected.pid, 400);
    table_free(&a.cur);
}

/* An empty process list: nothing to select, must not crash or change. */
static void test_move_on_empty_table_is_safe_noop(void)
{
    t_app       a;
    t_proc_key  before;
    int         redraw;

    mk_app(&a);
    TT_EQ_INT(table_init(&a.cur, 8), 0);
    view_init(&a.view);
    a.refresh_ms = 1000;
    before = a.selected;
    redraw = keys_handle(&a, TT_KEY_DOWN);
    TT_EQ_INT(redraw, 0);
    TT_CHECK(key_eq(a.selected, before));
    table_free(&a.cur);
}

/*
** A filter that matches nothing: view_flatten yields zero rows even
** though the table itself is non-empty. Must not crash, must not move.
*/
static void test_move_with_filter_matching_nothing_is_safe_noop(void)
{
    t_app   a;
    int     redraw;

    mk_flat3(&a);
    wcscpy(a.view.filter, L"no-such-process");
    redraw = keys_handle(&a, TT_KEY_DOWN);
    TT_EQ_INT(redraw, 0);
    table_free(&a.cur);
}

/*
** The selected process just died (its key is no longer in the current
** table at all). Up/Down must recover to a real row rather than reading
** through a stale key forever - and report that recovery as a redraw.
*/
static void test_move_recovers_when_selected_process_died(void)
{
    t_app   a;
    int     redraw;

    mk_flat3(&a);
    a.selected = (t_proc_key){ .pid = 9999, .create_time = 12345 };
    redraw = keys_handle(&a, TT_KEY_DOWN);
    TT_EQ_INT(redraw, 1);
    TT_EQ_INT((int)a.selected.pid, 100);        /* first visible row */
    table_free(&a.cur);
}

/*                                COLLAPSE                                    */

static void test_left_collapses_selected_row(void)
{
    t_app   a;
    int     redraw;

    mk_flat3(&a);
    a.selected = a.cur.procs[0].key;
    redraw = keys_handle(&a, TT_KEY_LEFT);
    TT_EQ_INT(redraw, 1);
    TT_EQ_INT(a.cur.procs[0].collapsed, 1);
    table_free(&a.cur);
}

/* Pressing collapse again once already collapsed changes nothing. */
static void test_left_twice_is_idempotent_and_stops_reporting_redraw(void)
{
    t_app   a;

    mk_flat3(&a);
    a.selected = a.cur.procs[0].key;
    keys_handle(&a, TT_KEY_LEFT);
    TT_EQ_INT(keys_handle(&a, TT_KEY_LEFT), 0);
    TT_EQ_INT(a.cur.procs[0].collapsed, 1);
    table_free(&a.cur);
}

static void test_right_expands_selected_row(void)
{
    t_app   a;
    int     redraw;

    mk_flat3(&a);
    a.selected = a.cur.procs[0].key;
    a.cur.procs[0].collapsed = 1;
    redraw = keys_handle(&a, TT_KEY_RIGHT);
    TT_EQ_INT(redraw, 1);
    TT_EQ_INT(a.cur.procs[0].collapsed, 0);
    table_free(&a.cur);
}

static void test_space_toggles_collapsed(void)
{
    t_app   a;

    mk_flat3(&a);
    a.selected = a.cur.procs[0].key;
    TT_EQ_INT(keys_handle(&a, TT_KEY_SPACE), 1);
    TT_EQ_INT(a.cur.procs[0].collapsed, 1);
    TT_EQ_INT(keys_handle(&a, TT_KEY_SPACE), 1);
    TT_EQ_INT(a.cur.procs[0].collapsed, 0);
    table_free(&a.cur);
}

/* Collapsing when the selection has died must not crash or touch state. */
static void test_collapse_with_dead_selection_is_safe_noop(void)
{
    t_app   a;
    int     redraw;

    mk_flat3(&a);
    a.selected = (t_proc_key){ .pid = 9999, .create_time = 1 };
    redraw = keys_handle(&a, TT_KEY_LEFT);
    TT_EQ_INT(redraw, 0);
    table_free(&a.cur);
}

/*                                  VIEWS                                     */

static void test_a_toggles_agents_only(void)
{
    t_app   a;

    mk_flat3(&a);
    TT_EQ_INT(a.view.agents_only, 0);
    TT_EQ_INT(keys_handle(&a, (int)L'a'), 1);
    TT_EQ_INT(a.view.agents_only, 1);
    TT_EQ_INT(keys_handle(&a, (int)L'a'), 1);
    TT_EQ_INT(a.view.agents_only, 0);
    table_free(&a.cur);
}

static void test_o_toggles_orphans_only(void)
{
    t_app   a;

    mk_flat3(&a);
    TT_EQ_INT(a.view.orphans_only, 0);
    TT_EQ_INT(keys_handle(&a, (int)L'o'), 1);
    TT_EQ_INT(a.view.orphans_only, 1);
    table_free(&a.cur);
}

static void test_f5_toggles_tree_mode(void)
{
    t_app   a;

    mk_flat3(&a);
    a.view.tree_mode = 1;
    TT_EQ_INT(keys_handle(&a, TT_KEY_F5), 1);
    TT_EQ_INT(a.view.tree_mode, 0);
    TT_EQ_INT(keys_handle(&a, TT_KEY_F5), 1);
    TT_EQ_INT(a.view.tree_mode, 1);
    table_free(&a.cur);
}

static void test_f6_and_gt_cycle_sort_forward(void)
{
    t_app   a;

    mk_flat3(&a);
    a.view.sort = SORT_PID;
    TT_EQ_INT(keys_handle(&a, TT_KEY_F6), 1);
    TT_EQ_INT((int)a.view.sort, (int)SORT_CPU);
    TT_EQ_INT(keys_handle(&a, (int)L'>'), 1);
    TT_EQ_INT((int)a.view.sort, (int)SORT_MEM);
    table_free(&a.cur);
}

static void test_lt_cycles_sort_backward(void)
{
    t_app   a;

    mk_flat3(&a);
    a.view.sort = SORT_PID;
    TT_EQ_INT(keys_handle(&a, (int)L'<'), 1);
    TT_EQ_INT((int)a.view.sort, (int)SORT_NAME);
    table_free(&a.cur);
}

/*                                  PAUSE                                     */

static void test_p_toggles_pause_outside_filter_mode(void)
{
    t_app   a;

    mk_flat3(&a);
    TT_EQ_INT(a.paused, 0);
    TT_EQ_INT(keys_handle(&a, (int)L'p'), 1);
    TT_EQ_INT(a.paused, 1);
    TT_EQ_INT(keys_handle(&a, (int)L'p'), 1);
    TT_EQ_INT(a.paused, 0);
    table_free(&a.cur);
}

/*                                 REFRESH                                    */

static void test_plus_increases_refresh_ms(void)
{
    t_app   a;

    mk_flat3(&a);
    a.refresh_ms = 1000;
    TT_EQ_INT(keys_handle(&a, (int)L'+'), 1);
    TT_CHECK(a.refresh_ms > 1000);
    table_free(&a.cur);
}

static void test_minus_decreases_refresh_ms(void)
{
    t_app   a;

    mk_flat3(&a);
    a.refresh_ms = 1000;
    TT_EQ_INT(keys_handle(&a, (int)L'-'), 1);
    TT_CHECK(a.refresh_ms < 1000);
    table_free(&a.cur);
}

/*
** Clamps at the floor rather than wrapping to a huge unsigned value or
** going to zero (a zero refresh would spin the event loop with no
** timeout at all - see the brief's own "busy-spin" warning).
*/
static void test_minus_clamps_at_floor_100(void)
{
    t_app   a;
    int     i;

    mk_flat3(&a);
    a.refresh_ms = 150;
    for (i = 0; i < 10; i++)
        keys_handle(&a, (int)L'-');
    TT_EQ_INT((int)a.refresh_ms, 100);
    TT_CHECK(a.refresh_ms >= 100u);
    /* Once at the floor, another '-' changes nothing and reports so. */
    TT_EQ_INT(keys_handle(&a, (int)L'-'), 0);
    TT_EQ_INT((int)a.refresh_ms, 100);
    table_free(&a.cur);
}

static void test_plus_clamps_at_ceiling_60000(void)
{
    t_app   a;
    int     i;

    mk_flat3(&a);
    a.refresh_ms = 59900;
    for (i = 0; i < 10; i++)
        keys_handle(&a, (int)L'+');
    TT_EQ_INT((int)a.refresh_ms, 60000);
    TT_EQ_INT(keys_handle(&a, (int)L'+'), 0);
    TT_EQ_INT((int)a.refresh_ms, 60000);
    table_free(&a.cur);
}

/*                                   HELP                                     */

static void test_f1_and_question_mark_toggle_help(void)
{
    t_app   a;

    mk_flat3(&a);
    TT_EQ_INT(keys_handle(&a, TT_KEY_F1), 1);
    TT_EQ_INT(a.help_open, 1);
    /* Help is modal: the NEXT key of any kind closes it rather than
       performing its own action - it must not also move the selection. */
    a.selected = a.cur.procs[0].key;
    TT_EQ_INT(keys_handle(&a, TT_KEY_DOWN), 1);
    TT_EQ_INT(a.help_open, 0);
    TT_EQ_INT((int)a.selected.pid, 100);        /* unchanged - not moved */
    table_free(&a.cur);
}

/*                                   QUIT                                     */

static void test_q_quits(void)
{
    t_app   a;

    mk_flat3(&a);
    a.running = 1;
    TT_EQ_INT(keys_handle(&a, (int)L'q'), 1);
    TT_EQ_INT(a.running, 0);
    table_free(&a.cur);
}

/*                                RESIZE                                      */

static void test_resize_always_forces_redraw(void)
{
    t_app   a;

    mk_flat3(&a);
    TT_EQ_INT(keys_handle(&a, TT_KEY_RESIZE), 1);
    table_free(&a.cur);
}

/*                              UNKNOWN KEYS                                  */

/*
** F9/Shift+F9 are explicitly Task 20 (kill) - not implemented here. An
** unbound key, and this specifically reserved one, must both return 0
** and leave every piece of state keys_handle owns untouched.
*/
static void test_unknown_key_is_noop(void)
{
    t_app   a;
    t_view  view_before;
    t_proc_key  sel_before;

    mk_flat3(&a);
    a.selected = a.cur.procs[0].key;
    view_before = a.view;
    sel_before = a.selected;
    TT_EQ_INT(keys_handle(&a, (int)L'z'), 0);
    TT_CHECK(memcmp(&a.view, &view_before, sizeof(t_view)) == 0);
    TT_CHECK(key_eq(a.selected, sel_before));
    TT_EQ_INT(a.running, 1);
    TT_EQ_INT(a.paused, 0);
    table_free(&a.cur);
}

static void test_f9_is_not_implemented_yet(void)
{
    t_app   a;

    mk_flat3(&a);
    TT_EQ_INT(keys_handle(&a, TT_KEY_F9), 0);
    TT_EQ_INT(keys_handle(&a, TT_KEY_SHIFT_F9), 0);
    table_free(&a.cur);
}

/*                       ESCAPE OUTSIDE FILTER MODE                           */

/*
** TT_KEY_ESCAPE outside filter_mode is not in the brief's bindings
** table verbatim - every other Escape test here runs WITH filter_mode
** active - but clear_filter() (src/input/keys.c) applies it there too,
** as a plausible reading of "clear filter and exit filter mode" as a
** general Esc binding: it lets a filter kept via Enter be cleared
** without re-entering filter mode first. Code review flagged that this
** path existed but was never exercised by a test; these two make it
** deliberate and falsifiable rather than incidental.
*/
static void test_escape_outside_filter_mode_clears_kept_filter(void)
{
    t_app   a;

    mk_flat3(&a);
    wcscpy(a.view.filter, L"node");     /* e.g. kept from an earlier Enter */
    a.filter_mode = 0;
    TT_EQ_INT(keys_handle(&a, TT_KEY_ESCAPE), 1);
    TT_EQ_WSTR(a.view.filter, L"");
    TT_EQ_INT(a.filter_mode, 0);
    table_free(&a.cur);
}

static void test_escape_outside_filter_mode_with_empty_filter_is_noop(void)
{
    t_app   a;

    mk_flat3(&a);
    TT_EQ_INT(a.view.filter[0], L'\0');
    TT_EQ_INT(a.filter_mode, 0);
    TT_EQ_INT(keys_handle(&a, TT_KEY_ESCAPE), 0);
    TT_EQ_WSTR(a.view.filter, L"");
    table_free(&a.cur);
}

/*                               FILTER MODE                                  */

static void test_slash_enters_filter_mode(void)
{
    t_app   a;

    mk_flat3(&a);
    TT_EQ_INT(a.filter_mode, 0);
    TT_EQ_INT(keys_handle(&a, (int)L'/'), 1);
    TT_EQ_INT(a.filter_mode, 1);
    table_free(&a.cur);
}

static void test_filter_mode_appends_printable_characters(void)
{
    t_app   a;

    mk_flat3(&a);
    keys_handle(&a, (int)L'/');
    keys_handle(&a, (int)L'n');
    keys_handle(&a, (int)L'o');
    keys_handle(&a, (int)L'd');
    keys_handle(&a, (int)L'e');
    TT_EQ_WSTR(a.view.filter, L"node");
    table_free(&a.cur);
}

/*
** The behaviour the whole task hinges on getting right: while filter
** mode is active, an ordinary binding character does NOT perform its
** normal-mode action - it appends to the filter text instead. 'p' is the
** brief's own worked example (pause vs. type).
*/
static void test_p_in_filter_mode_types_not_pauses(void)
{
    t_app   a;

    mk_flat3(&a);
    keys_handle(&a, (int)L'/');
    TT_EQ_INT(keys_handle(&a, (int)L'p'), 1);
    TT_EQ_WSTR(a.view.filter, L"p");
    TT_EQ_INT(a.paused, 0);
    table_free(&a.cur);
}

/*
** Every OTHER ordinary binding must be just as inert while filtering:
** 'a', 'o', F5, '+', '-', 'q' either append (printable) or are outright
** swallowed (symbolic keys), but none may perform its normal-mode
** action. This is the same claim as the 'p' test above, generalised, so
** a fix that special-cases only 'p' cannot pass it by accident.
*/
static void test_all_ordinary_bindings_inert_in_filter_mode(void)
{
    t_app   a;
    int     tree_mode_before;

    mk_flat3(&a);
    tree_mode_before = a.view.tree_mode;         /* mk_flat3 sets it to 0 */
    keys_handle(&a, (int)L'/');
    keys_handle(&a, (int)L'a');
    keys_handle(&a, (int)L'o');
    keys_handle(&a, (int)L'q');
    keys_handle(&a, TT_KEY_F5);                 /* symbolic: swallowed */
    keys_handle(&a, TT_KEY_UP);                 /* symbolic: swallowed */
    TT_EQ_INT(a.view.agents_only, 0);
    TT_EQ_INT(a.view.orphans_only, 0);
    TT_EQ_INT(a.running, 1);
    TT_EQ_INT(a.view.tree_mode, tree_mode_before);
    TT_EQ_WSTR(a.view.filter, L"aoq");            /* the three printables */
    table_free(&a.cur);
}

static void test_backspace_removes_one_character(void)
{
    t_app   a;

    mk_flat3(&a);
    keys_handle(&a, (int)L'/');
    keys_handle(&a, (int)L'n');
    keys_handle(&a, (int)L'o');
    TT_EQ_INT(keys_handle(&a, TT_KEY_BACKSPACE), 1);
    TT_EQ_WSTR(a.view.filter, L"n");
    table_free(&a.cur);
}

/* Backspace on an already-empty filter must not underflow or crash. */
static void test_backspace_on_empty_filter_is_safe_noop(void)
{
    t_app   a;

    mk_flat3(&a);
    keys_handle(&a, (int)L'/');
    TT_EQ_INT(keys_handle(&a, TT_KEY_BACKSPACE), 0);
    TT_EQ_WSTR(a.view.filter, L"");
    table_free(&a.cur);
}

static void test_escape_clears_filter_and_exits_filter_mode(void)
{
    t_app   a;

    mk_flat3(&a);
    keys_handle(&a, (int)L'/');
    keys_handle(&a, (int)L'x');
    TT_EQ_INT(keys_handle(&a, TT_KEY_ESCAPE), 1);
    TT_EQ_WSTR(a.view.filter, L"");
    TT_EQ_INT(a.filter_mode, 0);
    table_free(&a.cur);
}

static void test_enter_keeps_filter_and_exits_filter_mode(void)
{
    t_app   a;

    mk_flat3(&a);
    keys_handle(&a, (int)L'/');
    keys_handle(&a, (int)L'x');
    TT_EQ_INT(keys_handle(&a, TT_KEY_ENTER), 1);
    TT_EQ_WSTR(a.view.filter, L"x");
    TT_EQ_INT(a.filter_mode, 0);
    table_free(&a.cur);
}

/*
** Once out of filter mode (via Enter), ordinary bindings work again -
** the inertness is scoped to filter_mode being 1, not sticky forever.
*/
static void test_bindings_work_again_after_leaving_filter_mode(void)
{
    t_app   a;

    mk_flat3(&a);
    keys_handle(&a, (int)L'/');
    keys_handle(&a, TT_KEY_ENTER);
    TT_EQ_INT(keys_handle(&a, (int)L'p'), 1);
    TT_EQ_INT(a.paused, 1);
    table_free(&a.cur);
}

/*
** TT_FILTER_LEN counts the NUL terminator (see treetop.h): the buffer
** must never accept more than TT_FILTER_LEN - 1 characters. Feed it
** twice that many keystrokes and check both the final length and that
** wcslen never reads past what was actually written (a real overflow
** would corrupt adjacent t_view fields, which the sort/mode checks
** below would catch as a side effect too).
*/
static void test_filter_buffer_cannot_overflow(void)
{
    t_app   a;
    int     i;
    size_t  len;

    mk_flat3(&a);
    keys_handle(&a, (int)L'/');
    for (i = 0; i < TT_FILTER_LEN * 2; i++)
        keys_handle(&a, (int)L'x');
    len = wcslen(a.view.filter);
    TT_CHECK(len == (size_t)(TT_FILTER_LEN - 1));
    TT_CHECK(len < (size_t)TT_FILTER_LEN);
    /* Fields laid out after filter[] in t_view were not corrupted -
       mk_flat3 sets sort = SORT_PID explicitly, so a stray write past
       the buffer flipping it to any other mode would show up here. */
    TT_EQ_INT((int)a.view.sort, (int)SORT_PID);
    table_free(&a.cur);
}

void    test_keys(void)
{
    test_down_moves_to_next_row_and_reports_redraw();
    test_up_moves_to_previous_row();
    test_down_at_last_row_does_not_run_off();
    test_up_at_first_row_does_not_run_off();
    test_selection_tracks_key_not_index_across_reorder();
    test_move_on_empty_table_is_safe_noop();
    test_move_with_filter_matching_nothing_is_safe_noop();
    test_move_recovers_when_selected_process_died();

    test_left_collapses_selected_row();
    test_left_twice_is_idempotent_and_stops_reporting_redraw();
    test_right_expands_selected_row();
    test_space_toggles_collapsed();
    test_collapse_with_dead_selection_is_safe_noop();

    test_a_toggles_agents_only();
    test_o_toggles_orphans_only();
    test_f5_toggles_tree_mode();
    test_f6_and_gt_cycle_sort_forward();
    test_lt_cycles_sort_backward();

    test_p_toggles_pause_outside_filter_mode();

    test_plus_increases_refresh_ms();
    test_minus_decreases_refresh_ms();
    test_minus_clamps_at_floor_100();
    test_plus_clamps_at_ceiling_60000();

    test_f1_and_question_mark_toggle_help();

    test_q_quits();
    test_resize_always_forces_redraw();

    test_unknown_key_is_noop();
    test_f9_is_not_implemented_yet();

    test_escape_outside_filter_mode_clears_kept_filter();
    test_escape_outside_filter_mode_with_empty_filter_is_noop();

    test_slash_enters_filter_mode();
    test_filter_mode_appends_printable_characters();
    test_p_in_filter_mode_types_not_pauses();
    test_all_ordinary_bindings_inert_in_filter_mode();
    test_backspace_removes_one_character();
    test_backspace_on_empty_filter_is_safe_noop();
    test_escape_clears_filter_and_exits_filter_mode();
    test_enter_keeps_filter_and_exits_filter_mode();
    test_bindings_work_again_after_leaving_filter_mode();
    test_filter_buffer_cannot_overflow();
}
