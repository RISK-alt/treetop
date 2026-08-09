/* tests/test_chrome.c */
#include "harness.h"
#include "render.h"
#include "theme.h"
#include "app.h"

#include <string.h>
#include <wchar.h>

/*
** Splits a NUL-terminated frame buffer into its "\r\n"-terminated lines
** and hands each one to a checker, same convention as
** test_draw_table.c's for_each_line - draw_header/draw_footer/draw_help
** all follow draw_meters' one-call-one-line style.
*/
static void for_each_line(const wchar_t *text,
                          void (*fn)(const wchar_t *line, void *ctx),
                          void *ctx)
{
    wchar_t buf[32768];
    wchar_t *line;
    wchar_t *tok_ctx;

    wcsncpy(buf, text, 32767);
    buf[32767] = L'\0';
    line = wcstok(buf, L"\n", &tok_ctx);
    while (line != NULL)
    {
        fn(line, ctx);
        line = wcstok(NULL, L"\n", &tok_ctx);
    }
}

static void check_width_exact_cb(const wchar_t *line, void *ctx)
{
    int cols = *(int *)ctx;

    TT_CHECK((int)visible_len(line) == cols);
}

static size_t   count_lines(const wchar_t *text)
{
    size_t  n;

    n = 0;
    while (*text != L'\0')
    {
        if (*text == L'\n')
            n++;
        text++;
    }
    return (n);
}

static void mk_app(t_app *a)
{
    memset(a, 0, sizeof(*a));
}

/*                                   HEADER                                   */

/*
** Every header line must be EXACTLY cols wide, at both limited states, at
** every width the brief calls out - including the pathological 1 and 0.
** A stub that pads to a WRONG width, or one that only pads sometimes,
** fails this the same way test_draw_table's own width sweep would.
*/
static void test_header_width_exact_at_every_col(void)
{
    t_app   a;
    t_frame f;
    int     widths[9] = { 120, 100, 79, 60, 40, 20, 10, 1, 0 };
    int     limited_vals[2] = { 0, 1 };
    int     i;
    int     j;

    mk_app(&a);
    a.cur.count = 42;
    i = 0;
    while (i < 9)
    {
        j = 0;
        while (j < 2)
        {
            TT_EQ_INT(frame_init(&f, 4096), 0);
            draw_header(&f, &a, widths[i], limited_vals[j], a.cur.count);
            f.buf[f.len] = L'\0';
            for_each_line(f.buf, check_width_exact_cb, &widths[i]);
            frame_free(&f);
            j++;
        }
        i++;
    }
}

/*
** "limited mode" must appear when limited is set and be ABSENT when it is
** not - checked at a width comfortably wide enough for the whole header,
** so this is a genuine presence/absence check, not a truncation
** coincidence. Also checks the process count is actually rendered,
** because "the header shows count and clock" has no test elsewhere.
*/
static void test_header_limited_marker_and_count(void)
{
    t_app   a;
    t_frame f;

    mk_app(&a);
    a.cur.count = 7;

    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_header(&f, &a, 120, 1, a.cur.count);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"limited mode") != NULL);
    TT_CHECK(wcsstr(f.buf, L"treetop") != NULL);
    TT_CHECK(wcsstr(f.buf, L"7 procs") != NULL);
    frame_free(&f);

    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_header(&f, &a, 120, 0, a.cur.count);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"limited mode") == NULL);
    TT_CHECK(wcsstr(f.buf, L"treetop") != NULL);
    frame_free(&f);
}

/*
** When the view is narrowed (visible_count != a->cur.count - a text
** filter here, but the same logic covers agents-only/orphans-only) the
** header shows "N / M procs" instead of the bare total: a header
** claiming the full count while the table shows a fraction of it is the
** exact inconsistency this decision closes. Checked both directions so a
** stub that always shows the ratio (or never does) is caught either way.
*/
static void test_header_shows_ratio_when_narrowed(void)
{
    t_app   a;
    t_frame f;

    mk_app(&a);
    a.cur.count = 300;

    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_header(&f, &a, 120, 0, 12);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"12 / 300 procs") != NULL);
    frame_free(&f);

    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_header(&f, &a, 120, 0, 300);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"300 procs") != NULL);
    TT_CHECK(wcsstr(f.buf, L"/") == NULL);
    frame_free(&f);
}

/*                                   FOOTER                                   */

static void test_footer_width_exact_at_every_col(void)
{
    t_app   a;
    t_frame f;
    int     widths[9] = { 120, 100, 79, 60, 40, 20, 10, 1, 0 };
    int     i;

    mk_app(&a);
    i = 0;
    while (i < 9)
    {
        TT_EQ_INT(frame_init(&f, 4096), 0);
        draw_footer(&f, &a, widths[i]);
        f.buf[f.len] = L'\0';
        for_each_line(f.buf, check_width_exact_cb, &widths[i]);
        frame_free(&f);

        wcscpy(a.view.filter, L"needle");
        TT_EQ_INT(frame_init(&f, 4096), 0);
        draw_footer(&f, &a, widths[i]);
        f.buf[f.len] = L'\0';
        for_each_line(f.buf, check_width_exact_cb, &widths[i]);
        frame_free(&f);
        a.view.filter[0] = L'\0';

        i++;
    }
}

/*
** The footer shows the key bar when no filter is active, and the "/"
** prompt with the live filter text and a cursor when one is. Checked
** both directions so a stub permanently stuck in either state is caught.
*/
static void test_footer_filter_vs_keybar(void)
{
    t_app   a;
    t_frame f;

    mk_app(&a);
    wcscpy(a.view.filter, L"zzqueryXYZ");
    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_footer(&f, &a, 60);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"zzqueryXYZ") != NULL);
    TT_CHECK(wcsstr(f.buf, TT_INVERT) != NULL);
    TT_CHECK(wcsstr(f.buf, L"quit") == NULL);
    frame_free(&f);

    mk_app(&a);
    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_footer(&f, &a, 60);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"quit") != NULL);
    TT_CHECK(wcsstr(f.buf, L"help") != NULL);
    TT_CHECK(wcsstr(f.buf, TT_INVERT) == NULL);
    frame_free(&f);
}

/*
** Task 20: a->kill_status, once set (by src/main.c after a failed
** plat_kill()), replaces the key bar with the failure text - the two
** literal messages the brief names for -1 and -2. Checked with no active
** filter, since draw_footer's own precedence puts the filter first;
** test_footer_kill_status_vs_keybar_precedence right below covers that
** ordering directly.
*/
static void test_footer_shows_kill_status(void)
{
    t_app   a;
    t_frame f;

    mk_app(&a);
    wcscpy(a.kill_status, L"access denied - try running as administrator");
    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_footer(&f, &a, 60);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"access denied") != NULL);
    TT_CHECK(wcsstr(f.buf, L"quit") == NULL);
    frame_free(&f);

    mk_app(&a);
    wcscpy(a.kill_status, L"process already exited");
    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_footer(&f, &a, 60);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"process already exited") != NULL);
    TT_CHECK(wcsstr(f.buf, L"quit") == NULL);
    frame_free(&f);
}

/*
** A live filter always wins over a stale kill_status - both being
** non-empty at once is possible (the user filtered, killed something
** earlier, then filtered again) and the filter is what needs to stay
** editable. Proven by setting BOTH and checking only the filter's own
** markers show.
*/
static void test_footer_kill_status_vs_filter_precedence(void)
{
    t_app   a;
    t_frame f;

    mk_app(&a);
    wcscpy(a.kill_status, L"access denied - try running as administrator");
    wcscpy(a.view.filter, L"needle");
    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_footer(&f, &a, 60);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"needle") != NULL);
    TT_CHECK(wcsstr(f.buf, TT_INVERT) != NULL);
    TT_CHECK(wcsstr(f.buf, L"access denied") == NULL);
    frame_free(&f);
}

/* Same width sweep test_footer_width_exact_at_every_col already runs for
   the filter/keybar branches, extended to the new kill_status branch. */
static void test_footer_kill_status_width_exact_at_every_col(void)
{
    t_app   a;
    t_frame f;
    int     widths[9] = { 120, 100, 79, 60, 40, 20, 10, 1, 0 };
    int     i;

    mk_app(&a);
    wcscpy(a.kill_status, L"access denied - try running as administrator");
    i = 0;
    while (i < 9)
    {
        TT_EQ_INT(frame_init(&f, 4096), 0);
        draw_footer(&f, &a, widths[i]);
        f.buf[f.len] = L'\0';
        for_each_line(f.buf, check_width_exact_cb, &widths[i]);
        frame_free(&f);
        i++;
    }
}

/*                                    HELP                                    */

static void test_help_width_exact_at_every_col(void)
{
    t_frame f;
    int     widths[9] = { 120, 100, 79, 60, 40, 20, 10, 1, 0 };
    int     i;

    i = 0;
    while (i < 9)
    {
        TT_EQ_INT(frame_init(&f, 16384), 0);
        draw_help(&f, widths[i], 40);
        f.buf[f.len] = L'\0';
        for_each_line(f.buf, check_width_exact_cb, &widths[i]);
        TT_EQ_INT((int)count_lines(f.buf), 40);
        frame_free(&f);
        i++;
    }
}

/*
** Every binding named in the brief must be listed - checked on several
** distinct entries spread across the whole list, not just the first, so
** a stub that only renders the first few bindings (or a hard-coded
** constant block) cannot pass by accident.
*/
static void test_help_lists_every_binding(void)
{
    t_frame f;

    TT_EQ_INT(frame_init(&f, 16384), 0);
    draw_help(&f, 100, 40);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"move selection") != NULL);
    TT_CHECK(wcsstr(f.buf, L"collapse") != NULL);
    TT_CHECK(wcsstr(f.buf, L"agents-only") != NULL);
    TT_CHECK(wcsstr(f.buf, L"orphans-only") != NULL);
    TT_CHECK(wcsstr(f.buf, L"clear filter") != NULL);
    TT_CHECK(wcsstr(f.buf, L"sort column") != NULL);
    TT_CHECK(wcsstr(f.buf, L"kill subtree") != NULL);
    TT_CHECK(wcsstr(f.buf, L"refresh interval") != NULL);
    TT_CHECK(wcsstr(f.buf, L"quit") != NULL);
    frame_free(&f);
}

/*
** Decision: when rows is too small to fit every binding, rows are
** dropped from the BOTTOM of the list (title first, then bindings in
** brief order), never truncated mid-line, and the overlay still emits
** exactly `rows` lines each exactly `cols` wide. rows=8 leaves room for
** only the top few bindings - proving this actually drops content (not
** just shrinks the box cosmetically) is what makes this falsifiable: a
** draw_help that always renders the full 18-line box regardless of rows
** would fail the exact-line-count check below, and one that renders the
** full list unconditionally would fail the "late binding is absent"
** check.
*/
static void test_help_degrades_when_rows_too_small(void)
{
    t_frame f;

    TT_EQ_INT(frame_init(&f, 16384), 0);
    draw_help(&f, 120, 8);
    f.buf[f.len] = L'\0';
    TT_EQ_INT((int)count_lines(f.buf), 8);
    {
        int cols = 120;
        for_each_line(f.buf, check_width_exact_cb, &cols);
    }
    TT_CHECK(wcsstr(f.buf, L"treetop") != NULL);
    TT_CHECK(wcsstr(f.buf, L"move selection") != NULL);
    TT_CHECK(wcsstr(f.buf, L"quit") == NULL);
    TT_CHECK(wcsstr(f.buf, L"kill subtree") == NULL);
    TT_CHECK(wcsstr(f.buf, L"press any key") == NULL);
    frame_free(&f);
}

/*
** Decision: when cols is too small for the box's natural width, the box
** itself shrinks to fit cols and each content line is right-truncated
** with a trailing ellipsis (never dropped, never overflowing) rather
** than wrapped. Checked by confirming a long action string no longer
** appears in full, while SOME ellipsis shows up to prove truncation
** actually ran rather than the line simply being silently dropped.
*/
static void test_help_degrades_when_cols_too_small(void)
{
    t_frame f;

    TT_EQ_INT(frame_init(&f, 16384), 0);
    draw_help(&f, 20, 40);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"orphans-only view") == NULL);
    TT_CHECK(wcsstr(f.buf, L"\u2026") != NULL);
    {
        int cols = 20;
        for_each_line(f.buf, check_width_exact_cb, &cols);
    }
    TT_EQ_INT((int)count_lines(f.buf), 40);
    frame_free(&f);
}

/*
** Decision: below a minimum viable box size (cols < 5, or rows < 3) the
** overlay refuses to draw a box at all - every line is blank spaces,
** still exactly `rows` lines of exactly `cols` width each, rather than a
** malformed or negative-width box. Checked on both axes independently.
*/
static void test_help_refuses_below_minimum(void)
{
    t_frame f;

    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_help(&f, 4, 40);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcschr(f.buf, L'\u250c') == NULL);
    TT_EQ_INT((int)count_lines(f.buf), 40);
    {
        int cols = 4;
        for_each_line(f.buf, check_width_exact_cb, &cols);
    }
    frame_free(&f);

    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_help(&f, 120, 2);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcschr(f.buf, L'\u250c') == NULL);
    TT_EQ_INT((int)count_lines(f.buf), 2);
    frame_free(&f);
}

/*                                  CONFIRM                                   */

/*
** N standalone processes (no tree relationship needed - draw_confirm()
** just draws whatever t_process* array it is handed, in the order
** given) with distinctive, greppable image names.
*/
static void mk_victims(t_table *t, t_process **rows, int n)
{
    t_process   p;
    wchar_t     name[32];
    int         i;

    table_init(t, 8);
    i = 0;
    while (i < n)
    {
        swprintf(name, 32, L"victim%d.exe", i);
        p = mk_proc((unsigned long)(500 + i), 5000 + (unsigned long long)i,
                4, name);
        table_add(t, &p);
        i++;
    }
    for (i = 0; i < n; i++)
        rows[i] = &t->procs[i];
}

/*
** Every line draw_confirm() emits must be exactly `cols` visible
** characters, at every width the brief calls out - the same sweep
** test_help_width_exact_at_every_col already runs for draw_help(), and
** the same defect class the brief warns three prior tasks shipped
** ("Three tasks in a row shipped a defect here before it was caught").
*/
static void test_confirm_width_exact_at_every_col(void)
{
    t_table     t;
    t_process   *rows[4];
    t_frame     f;
    int         widths[9] = { 120, 100, 79, 60, 40, 20, 10, 1, 0 };
    int         i;

    mk_victims(&t, rows, 3);
    i = 0;
    while (i < 9)
    {
        TT_EQ_INT(frame_init(&f, 16384), 0);
        draw_confirm(&f, L"kill subtree - 3 processes?", rows, 3,
                widths[i], 20);
        f.buf[f.len] = L'\0';
        for_each_line(f.buf, check_width_exact_cb, &widths[i]);
        TT_EQ_INT((int)count_lines(f.buf), 20);
        frame_free(&f);
        i++;
    }
    table_free(&t);
}

/*
** With ample room, the title, every victim's PID and image, and the
** y/N prompt must all be present in the rendered box.
*/
static void test_confirm_lists_title_and_every_victim(void)
{
    t_table     t;
    t_process   *rows[3];
    t_frame     f;

    mk_victims(&t, rows, 3);
    TT_EQ_INT(frame_init(&f, 16384), 0);
    draw_confirm(&f, L"kill subtree - 3 processes?", rows, 3, 100, 20);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"kill subtree - 3 processes?") != NULL);
    TT_CHECK(wcsstr(f.buf, L"victim0.exe") != NULL);
    TT_CHECK(wcsstr(f.buf, L"victim1.exe") != NULL);
    TT_CHECK(wcsstr(f.buf, L"victim2.exe") != NULL);
    TT_CHECK(wcsstr(f.buf, L"500") != NULL);
    TT_CHECK(wcsstr(f.buf, L"Terminate? [y/N]") != NULL);
    frame_free(&f);
    table_free(&t);
}

/*
** More victims than fit in the available rows: the visible list is
** bounded and the remainder collapses into a single "... and N more"
** line, rather than either overflowing `rows` or silently vanishing.
** rows=8 against 30 victims leaves room for only a handful of victim
** lines (box_h capped at 8: 2 borders + title + prompt + up to 4 victim
** slots, one of which becomes the "+more" line) - proven by checking an
** early victim IS listed, a late one is NOT, and the "+more" text IS
** present, while the total line count and every line's width still hold
** exactly.
*/
static void test_confirm_degrades_with_more_indicator_when_rows_small(void)
{
    t_table     t;
    t_process   *rows[30];
    t_frame     f;

    mk_victims(&t, rows, 30);
    TT_EQ_INT(frame_init(&f, 16384), 0);
    draw_confirm(&f, L"kill subtree - 30 processes?", rows, 30, 80, 8);
    f.buf[f.len] = L'\0';
    TT_EQ_INT((int)count_lines(f.buf), 8);
    {
        int cols = 80;
        for_each_line(f.buf, check_width_exact_cb, &cols);
    }
    TT_CHECK(wcsstr(f.buf, L"kill subtree - 30 processes?") != NULL);
    TT_CHECK(wcsstr(f.buf, L"victim0.exe") != NULL);
    TT_CHECK(wcsstr(f.buf, L"victim29.exe") == NULL);
    TT_CHECK(wcsstr(f.buf, L"more") != NULL);
    TT_CHECK(wcsstr(f.buf, L"Terminate? [y/N]") != NULL);
    frame_free(&f);
    table_free(&t);
}

/*
** Ambiguity resolution #3 from the brief, verbatim: "If the confirmation
** overlay cannot fit even one victim line, still show the count and the
** y/N prompt - never a bare 'confirm?' with no idea what dies." rows=4
** is the smallest box draw_confirm() will still draw at all (2 borders +
** title + prompt, with ZERO room left for any victim or "+more" line);
** the title here carries the count since draw_confirm() itself never
** computes one - proving the caller-supplied count survives even though
** every victim line is dropped.
*/
static void test_confirm_shows_title_and_prompt_with_zero_room_for_victims(void)
{
    t_table     t;
    t_process   *rows[10];
    t_frame     f;

    mk_victims(&t, rows, 10);
    TT_EQ_INT(frame_init(&f, 16384), 0);
    draw_confirm(&f, L"kill subtree - 10 processes?", rows, 10, 80, 4);
    f.buf[f.len] = L'\0';
    TT_EQ_INT((int)count_lines(f.buf), 4);
    TT_CHECK(wcsstr(f.buf, L"kill subtree - 10 processes?") != NULL);
    TT_CHECK(wcsstr(f.buf, L"Terminate? [y/N]") != NULL);
    TT_CHECK(wcsstr(f.buf, L"victim0.exe") == NULL);
    {
        int cols = 80;
        for_each_line(f.buf, check_width_exact_cb, &cols);
    }
    frame_free(&f);
    table_free(&t);
}

/*
** Below the minimum viable box (cols < 5, or rows < 4 - one more than
** draw_help()'s own rows < 3, since this box always reserves BOTH a
** title and a prompt line, not just one guaranteed content row) the
** overlay refuses to draw a box at all: blank lines only, still exactly
** `rows` lines of exactly `cols` width. Checked on both axes
** independently, mirroring test_help_refuses_below_minimum.
*/
static void test_confirm_refuses_below_minimum(void)
{
    t_table     t;
    t_process   *rows[3];
    t_frame     f;

    mk_victims(&t, rows, 3);

    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_confirm(&f, L"kill process?", rows, 3, 4, 20);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcschr(f.buf, L'\u250c') == NULL);
    TT_EQ_INT((int)count_lines(f.buf), 20);
    {
        int cols = 4;
        for_each_line(f.buf, check_width_exact_cb, &cols);
    }
    frame_free(&f);

    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_confirm(&f, L"kill process?", rows, 3, 80, 3);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcschr(f.buf, L'\u250c') == NULL);
    TT_EQ_INT((int)count_lines(f.buf), 3);
    frame_free(&f);

    table_free(&t);
}

/*
** n == 0 (every confirmed victim has already exited by render time - see
** render_all's own confirm_resolve_and_draw()) must not crash: the box
** still draws with just the caller's title and the prompt.
*/
static void test_confirm_zero_victims_is_safe(void)
{
    t_frame f;

    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_confirm(&f, L"kill subtree - 0 processes?", NULL, 0, 80, 20);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"kill subtree - 0 processes?") != NULL);
    TT_CHECK(wcsstr(f.buf, L"Terminate? [y/N]") != NULL);
    TT_EQ_INT((int)count_lines(f.buf), 20);
    frame_free(&f);
}

/*
** A single F9 kill's title embeds the victim's own image name
** (confirm_resolve_and_draw()'s "kill process %lu (%ls)?"), which can be
** up to TT_IMAGE_LEN - 1 (63) characters - worst case a ~90-character
** title. On a wide enough terminal (cols=200, comfortably past 90+4)
** that whole title must render VERBATIM, not silently cut off: the
** internal buffer draw_confirm()/overlay_trunc() share to build a
** content row must be sized to hold it, not just the "typical" short
** title every other test here happens to use. A too-small internal
** buffer would truncate the tail of the image name without any ellipsis
** marker (overlay_trunc's own "already fits" branch trusts the visible
** width without checking destination buffer capacity), which is exactly
** the class of defect three prior tasks shipped per the brief - this is
** what makes that failure mode concrete for draw_confirm() specifically,
** not just re-asserted for draw_help()'s already-short content lines.
*/
static void test_confirm_long_title_renders_in_full_on_wide_terminal(void)
{
    t_frame     f;
    wchar_t     longname[64];
    wchar_t     title[128];
    int         i;

    i = 0;
    while (i < 63)
    {
        longname[i] = (wchar_t)(L'a' + (i % 26));
        i++;
    }
    longname[63] = L'\0';
    swprintf(title, 128, L"kill process 4294967295 (%ls)?", longname);
    TT_CHECK((int)wcslen(title) >= 85);    /* sanity: this IS the worst case */

    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_confirm(&f, title, NULL, 0, 200, 20);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, longname) != NULL);      /* full name present */
    TT_CHECK(wcsstr(f.buf, L"4294967295") != NULL);
    {
        int cols = 200;
        for_each_line(f.buf, check_width_exact_cb, &cols);
    }
    frame_free(&f);

    /* Same title, narrow terminal: NOW it must truncate WITH an
       ellipsis, proving the wide-terminal case above is a real "fits"
       branch and not a coincidence of overlay_trunc always truncating
       regardless. */
    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_confirm(&f, title, NULL, 0, 50, 20);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, longname) == NULL);
    TT_CHECK(wcsstr(f.buf, L"\u2026") != NULL);
    {
        int cols = 50;
        for_each_line(f.buf, check_width_exact_cb, &cols);
    }
    frame_free(&f);
}

/*
** Code review finding: the previous test above proves draw_confirm()
** survives TODAY's worst-case title (a 63-char image name, ~90
** characters total) because its shared internal buffer happens to be
** sized above that. That is safety by coincidence of buffer sizing, not
** by construction - draw_confirm() takes an arbitrary caller-supplied
** `title` with no enforced bound, so a future caller (or TT_IMAGE_LEN
** simply growing past its current 64) reproduces the identical silent-
** truncation-without-ellipsis defect against a bigger buffer.
**
** This test is deliberately independent of whatever the internal buffer
** size happens to be today or in the future: a 1000-character title,
** three orders of magnitude past any real title this codebase composes,
** on a terminal wide enough (cols=1100, comfortably past the box's own
** 1000+4=1004 natural width so cols itself is not what does the
** clamping) that the box's own width math would - before this fix -
** size the box to fit the whole title and take overlay_trunc()'s
** "already fits" branch. The fix under test is the clamp INSIDE
** overlay_trunc() itself (against its own destination buffer capacity,
** not against the caller's requested width), so this must still produce
** a correctly ellipsis-truncated, exactly-`cols`-wide line - not a
** silently cut-off one, and not a buffer overrun - no matter how large
** TT_CONFIRM_LINE_BUF is ever set to. rows is kept modest (15) so the
** rendered output stays comfortably under for_each_line()'s own 32768
** wchar_t scratch buffer - this is a test of draw_confirm(), not of
** exhausting that shared test helper's own separate, unrelated capacity.
*/
static void test_confirm_title_longer_than_any_buffer_still_ellipsizes(void)
{
    t_frame     f;
    wchar_t     title[1024];
    int         i;

    i = 0;
    while (i < 1000)
    {
        title[i] = (wchar_t)(L'a' + (i % 26));
        i++;
    }
    title[1000] = L'\0';

    TT_EQ_INT(frame_init(&f, 16384), 0);
    draw_confirm(&f, title, NULL, 0, 1100, 15);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, title) == NULL);        /* NOT verbatim */
    TT_CHECK(wcsstr(f.buf, L"\u2026") != NULL);     /* properly ellipsized */
    {
        int cols = 1100;
        for_each_line(f.buf, check_width_exact_cb, &cols);
    }
    TT_EQ_INT((int)count_lines(f.buf), 15);
    frame_free(&f);
}

/*                                 RENDER_ALL                                 */

/*
** render_all must compose header, meters, table and footer IN THAT
** ORDER - checked by finding a distinctive marker unique to each section
** and asserting their positions in the buffer strictly increase. A
** render_all that reordered sections (or dropped one) would fail this
** even though each section's own unit tests still pass individually.
*/
static void test_render_all_order_and_line_budget(void)
{
    t_app       a;
    t_process   p;
    t_frame     f;
    wchar_t     *pos_title;
    wchar_t     *pos_cpu;
    wchar_t     *pos_row;
    wchar_t     *pos_quit;
    int         i;
    int         lines;

    mk_app(&a);
    TT_EQ_INT(table_init(&a.cur, 32), 0);
    TT_EQ_INT(table_init(&a.prev, 32), 0);
    view_init(&a.view);
    i = 0;
    while (i < 30)
    {
        p = mk_proc((unsigned long)(100 + i), 1000 + (unsigned long long)i,
                4, L"proc.exe");
        p.cmdline = L"proc.exe --flag";
        if (i == 5)
        {
            p = mk_proc(105, 1005, 4, L"zzztargetproc.exe");
            p.cmdline = L"zzztargetproc.exe --marker";
        }
        table_add(&a.cur, &p);
        i++;
    }
    tree_build(&a.cur);
    a.sys.core_count = 4;
    a.sys.cpu_pct = 10.0;
    a.sys.mem_total = 1024ULL * 1024 * 1024;
    a.sys.mem_used = 512ULL * 1024 * 1024;

    TT_EQ_INT(frame_init(&f, 65536), 0);
    render_all(&f, &a, 120, 30, 0);
    f.buf[f.len] = L'\0';

    pos_title = wcsstr(f.buf, L"treetop");
    pos_cpu = wcsstr(f.buf, L"CPU");
    pos_row = wcsstr(f.buf, L"zzztargetproc");
    pos_quit = wcsstr(f.buf, L"quit");

    TT_CHECK(pos_title != NULL);
    TT_CHECK(pos_cpu != NULL);
    TT_CHECK(pos_row != NULL);
    TT_CHECK(pos_quit != NULL);
    if (pos_title && pos_cpu && pos_row && pos_quit)
    {
        TT_CHECK(pos_title < pos_cpu);
        TT_CHECK(pos_cpu < pos_row);
        TT_CHECK(pos_row < pos_quit);
    }

    /* Home-and-erase opens every frame: ESC [ H ESC [ 2 J, 7 chars. */
    TT_CHECK(wcsncmp(f.buf, L"\x1b[H\x1b[2J", 7) == 0);

    /* header(1) + meters(3, cols>=100) + table(25) + footer(1) == 30. */
    lines = (int)count_lines(f.buf);
    TT_EQ_INT(lines, 30);

    table_free(&a.cur);
    table_free(&a.prev);
    frame_free(&f);
}

/*
** render_all's very first LINE (up to the first '\r') carries
** "\x1b[H\x1b[2J" (cursor-home + erase-display) immediately before the
** header content, on the SAME line - not its own line, since neither
** escape moves the cursor down a row on a real terminal, and adding a
** "\r\n" after it just to give it a line of its own would actually push
** the header down a row on screen, which is wrong. That means the
** header's own width-exactness invariant has to hold on a line that
** mixes a non-SGR escape with real header content, which is exactly the
** case visible_len's old 'm'-only stripping rule got wrong (see
** test_frame.c's updated comment) - this is what a fix to that helper
** without a test exercising THIS composed line would have left
** unverified.
**
** Deliberately checks ONLY this first line, not every line render_all
** emits: draw_meters' per-core strip (drawn whenever cols >= 100) is
** documented and tested elsewhere (test_frame.c's test_meters_bounded)
** to be bounded by cols, not padded to it exactly - that is a Task 16
** contract this task does not re-open. Blanket-checking every line here
** would wrongly demand a guarantee draw_meters never made.
*/
static void test_render_all_first_line_width(void)
{
    t_app       a;
    t_process   p;
    t_frame     f;
    wchar_t     buf[512];
    wchar_t     *first_line;
    wchar_t     *tok_ctx;
    int         widths[3] = { 120, 79, 40 };
    int         i;

    mk_app(&a);
    TT_EQ_INT(table_init(&a.cur, 8), 0);
    TT_EQ_INT(table_init(&a.prev, 8), 0);
    view_init(&a.view);
    p = mk_proc(100, 1000, 4, L"a.exe");
    p.cmdline = L"a.exe --flag";
    table_add(&a.cur, &p);
    tree_build(&a.cur);
    a.sys.core_count = 2;
    a.sys.mem_total = 1024;
    a.sys.mem_used = 512;

    i = 0;
    while (i < 3)
    {
        TT_EQ_INT(frame_init(&f, 65536), 0);
        render_all(&f, &a, widths[i], 20, 0);
        f.buf[f.len] = L'\0';
        wcsncpy(buf, f.buf, 511);
        buf[511] = L'\0';
        first_line = wcstok(buf, L"\n", &tok_ctx);
        TT_CHECK(first_line != NULL);
        if (first_line != NULL)
            check_width_exact_cb(first_line, &widths[i]);
        frame_free(&f);
        i++;
    }

    table_free(&a.cur);
    table_free(&a.prev);
}

/*
** render_all must never emit more than `rows` lines, even when `rows` is
** too small to fit header + meters + footer's combined 1+3+1 floor (at
** cols >= 100). This is the concrete case the review named: rows=3,
** cols=120 used to produce 1+3+0+1 = 5 lines against a 3-row budget,
** because table_rows alone was clamped to 0 while header/meters/footer
** were still drawn unconditionally. The priority order documented above
** render_all (footer, then header, then meters, then table) means at
** rows=3 here meters is the one dropped: footer(1) + header(1) +
** table(1) = 3 exactly, with real row data available so that 1 is a
** genuine table row, not just an empty budget nobody used.
*/
static void test_render_all_rows_never_exceed_budget(void)
{
    t_app       a;
    t_process   p;
    t_frame     f;
    int         rows_vals[6] = { 5, 4, 3, 2, 1, 0 };
    int         i;
    int         lines;

    mk_app(&a);
    TT_EQ_INT(table_init(&a.cur, 8), 0);
    TT_EQ_INT(table_init(&a.prev, 8), 0);
    view_init(&a.view);
    i = 0;
    while (i < 5)
    {
        p = mk_proc((unsigned long)(100 + i), 1000 + (unsigned long long)i,
                4, L"a.exe");
        p.cmdline = L"a.exe --flag";
        table_add(&a.cur, &p);
        i++;
    }
    tree_build(&a.cur);
    a.sys.core_count = 2;
    a.sys.mem_total = 1024;
    a.sys.mem_used = 512;

    i = 0;
    while (i < 6)
    {
        TT_EQ_INT(frame_init(&f, 4096), 0);
        render_all(&f, &a, 120, rows_vals[i], 0);
        f.buf[f.len] = L'\0';
        lines = (int)count_lines(f.buf);
        TT_CHECK(lines <= rows_vals[i]);
        frame_free(&f);
        i++;
    }

    /* Exact counts where the outcome is fully determined. */
    TT_EQ_INT(frame_init(&f, 4096), 0);
    render_all(&f, &a, 120, 0, 0);
    f.buf[f.len] = L'\0';
    TT_EQ_INT((int)count_lines(f.buf), 0);
    frame_free(&f);

    TT_EQ_INT(frame_init(&f, 4096), 0);
    render_all(&f, &a, 120, 1, 0);
    f.buf[f.len] = L'\0';
    TT_EQ_INT((int)count_lines(f.buf), 1);
    TT_CHECK(wcsstr(f.buf, L"quit") != NULL);
    frame_free(&f);

    TT_EQ_INT(frame_init(&f, 4096), 0);
    render_all(&f, &a, 120, 3, 0);
    f.buf[f.len] = L'\0';
    TT_EQ_INT((int)count_lines(f.buf), 3);
    TT_CHECK(wcsstr(f.buf, L"CPU") == NULL);
    frame_free(&f);

    table_free(&a.cur);
    table_free(&a.prev);
}

/*                          RENDER_ALL SELECTION/SCROLL                       */

/*
** Shared fixture for the four scroll/centering tests below: 20 processes
** in flat, PID-ascending order (deterministic, no tree sort surprises),
** each carrying a unique "--tagNNN" marker in its command line so a test
** can look up exactly one row by substring. cmdbufs is caller-owned and
** must outlive every render_all() call the caller makes, since table_add
** copies t_process by value - including the raw cmdline pointer, not the
** string - so the backing storage has to stay alive as long as a->cur
** does.
*/
static void build_selection_fixture(t_app *a, wchar_t cmdbufs[20][32])
{
    t_process   p;
    int         i;

    mk_app(a);
    TT_EQ_INT(table_init(&a->cur, 32), 0);
    TT_EQ_INT(table_init(&a->prev, 32), 0);
    view_init(&a->view);
    a->view.tree_mode = 0;
    a->view.sort = SORT_PID;
    a->view.sort_desc = 0;
    i = 0;
    while (i < 20)
    {
        p = mk_proc((unsigned long)(100 + i), 1000 + (unsigned long long)i,
                4, L"proc.exe");
        swprintf(cmdbufs[i], 32, L"proc.exe --tag%d", 100 + i);
        p.cmdline = cmdbufs[i];
        table_add(&a->cur, &p);
        i++;
    }
    tree_build(&a->cur);
    a->sys.core_count = 1;
    a->sys.mem_total = 1024;
    a->sys.mem_used = 512;
}

/*
** Finds the '\r\n'-delimited line containing `needle` and reports
** whether TT_INVERT appears before that line ends: 1 (found, inverted -
** this is the selected row), 0 (found, not inverted - visible but not
** selected) or -1 (not found at all - scrolled out of the window
** entirely, not merely un-highlighted). That three-way split is what
** lets a single helper prove both "this row is selected" and "this row
** was scrolled off screen", which a plain wcsstr presence check cannot
** tell apart from "present but not the selected one".
*/
static int  line_invert_state(const wchar_t *buf, const wchar_t *needle)
{
    const wchar_t   *hit;
    const wchar_t   *start;
    const wchar_t   *nl;
    const wchar_t   *inv;

    hit = wcsstr(buf, needle);
    if (hit == NULL)
        return (-1);
    start = hit;
    while (start > buf && start[-1] != L'\n')
        start--;
    nl = wcschr(start, L'\n');
    if (nl == NULL)
        nl = start + wcslen(start);
    inv = wcsstr(start, TT_INVERT);
    return (inv != NULL && inv < nl);
}

/*
** cols=120 (meter_lines=3) and rows=13 -> footer(1) + header(1) +
** meters(3) + table(8) = 13 exactly, a known, deterministic 8-row
** window against 20 total rows.
*/
#define TT_SEL_TEST_COLS   120
#define TT_SEL_TEST_ROWS   13

/*
** Selecting the very first row must not scroll at all: centering would
** ask for top = 0 - window/2, which is negative and clamps to 0, leaving
** the window exactly [0, window). Proven by checking the selected row is
** inverted, the last row of that window is present-but-plain, and a row
** just past the window is entirely absent (scrolled out), not merely
** un-highlighted.
*/
static void test_render_all_selection_no_scroll_near_top(void)
{
    t_app       a;
    t_frame     f;
    wchar_t     cmdbufs[20][32];

    build_selection_fixture(&a, cmdbufs);
    a.selected.pid = 100;
    a.selected.create_time = 1000;

    TT_EQ_INT(frame_init(&f, 65536), 0);
    render_all(&f, &a, TT_SEL_TEST_COLS, TT_SEL_TEST_ROWS, 0);
    f.buf[f.len] = L'\0';

    TT_EQ_INT(line_invert_state(f.buf, L"--tag100"), 1);
    TT_EQ_INT(line_invert_state(f.buf, L"--tag107"), 0);
    TT_EQ_INT(line_invert_state(f.buf, L"--tag108"), -1);
    TT_EQ_INT(line_invert_state(f.buf, L"--tag119"), -1);

    frame_free(&f);
    table_free(&a.cur);
    table_free(&a.prev);
}

/*
** Selecting a row well clear of both ends must centre the window on it:
** top = sel - window/2 = 10 - 4 = 6, so the visible window is [6, 14),
** i.e. tag106..tag113 inclusive, with tag105 and tag114 - one row on
** either side of the window - both entirely absent.
*/
static void test_render_all_selection_centers_in_middle(void)
{
    t_app       a;
    t_frame     f;
    wchar_t     cmdbufs[20][32];

    build_selection_fixture(&a, cmdbufs);
    a.selected.pid = 110;
    a.selected.create_time = 1010;

    TT_EQ_INT(frame_init(&f, 65536), 0);
    render_all(&f, &a, TT_SEL_TEST_COLS, TT_SEL_TEST_ROWS, 0);
    f.buf[f.len] = L'\0';

    TT_EQ_INT(line_invert_state(f.buf, L"--tag110"), 1);
    TT_EQ_INT(line_invert_state(f.buf, L"--tag106"), 0);
    TT_EQ_INT(line_invert_state(f.buf, L"--tag113"), 0);
    TT_EQ_INT(line_invert_state(f.buf, L"--tag105"), -1);
    TT_EQ_INT(line_invert_state(f.buf, L"--tag114"), -1);

    frame_free(&f);
    table_free(&a.cur);
    table_free(&a.prev);
}

/*
** Selecting the very last row must not leave a half-empty page: a naive
** unclamped centre (top = 19 - 4 = 15) would only have 5 rows left
** (indices 15..19) to show against an 8-row window. The actual clamp
** (top = nrows - window = 20 - 8 = 12) keeps the window full - proven
** both by an exact line-count check (header+meters+footer+8 table rows
** == 13, the full budget, not fewer) and by checking the window starts
** exactly at tag112, not tag111 or tag115.
*/
static void test_render_all_selection_clamped_full_last_page(void)
{
    t_app       a;
    t_frame     f;

    wchar_t     cmdbufs[20][32];

    build_selection_fixture(&a, cmdbufs);
    a.selected.pid = 119;
    a.selected.create_time = 1019;

    TT_EQ_INT(frame_init(&f, 65536), 0);
    render_all(&f, &a, TT_SEL_TEST_COLS, TT_SEL_TEST_ROWS, 0);
    f.buf[f.len] = L'\0';

    TT_EQ_INT(line_invert_state(f.buf, L"--tag119"), 1);
    TT_EQ_INT(line_invert_state(f.buf, L"--tag112"), 0);
    TT_EQ_INT(line_invert_state(f.buf, L"--tag111"), -1);
    TT_EQ_INT((int)count_lines(f.buf), TT_SEL_TEST_ROWS);

    frame_free(&f);
    table_free(&a.cur);
    table_free(&a.prev);
}

/*
** A selected key that matches no row currently in the table (the
** process it named has since exited) must behave exactly like "nothing
** selected": no crash, no wild scroll (top stays 0, same window as the
** near-top case), and critically no row anywhere is inverted - proving
** sel really did fall back to the (size_t)-1 sentinel rather than
** matching some unrelated row by accident (e.g. a stray zero-initialised
** comparison).
*/
static void test_render_all_selection_absent_key_no_crash(void)
{
    t_app       a;
    t_frame     f;
    wchar_t     cmdbufs[20][32];

    build_selection_fixture(&a, cmdbufs);
    a.selected.pid = 999999;
    a.selected.create_time = 999999;

    TT_EQ_INT(frame_init(&f, 65536), 0);
    render_all(&f, &a, TT_SEL_TEST_COLS, TT_SEL_TEST_ROWS, 0);
    f.buf[f.len] = L'\0';

    TT_EQ_INT(line_invert_state(f.buf, L"--tag100"), 0);
    TT_CHECK(wcsstr(f.buf, TT_INVERT) == NULL);
    TT_EQ_INT((int)count_lines(f.buf), TT_SEL_TEST_ROWS);

    frame_free(&f);
    table_free(&a.cur);
    table_free(&a.prev);
}

/*                       RENDER_ALL CONFIRM OVERLAY                           */

/*
** While a->confirm_open, render_all() must draw ONLY the confirm
** overlay - no header, meters, table row or footer key bar. Checked by
** confirming a distinctive marker from EACH of those four sections is
** absent (a render_all that forgot the early return would still compose
** them underneath, invisible on a real terminal thanks to the leading
** home-and-erase but very much present in the buffer this test reads)
** while the overlay's own content - the prompt, at minimum - is present.
*/
static void test_render_all_confirm_open_shows_overlay_not_normal_content(void)
{
    t_app       a;
    t_process   p;
    t_frame     f;
    t_proc_key  victims[1];

    mk_app(&a);
    TT_EQ_INT(table_init(&a.cur, 8), 0);
    TT_EQ_INT(table_init(&a.prev, 8), 0);
    view_init(&a.view);
    p = mk_proc(100, 1000, 4, L"zzztargetproc.exe");
    table_add(&a.cur, &p);
    tree_build(&a.cur);
    a.sys.core_count = 4;

    victims[0] = a.cur.procs[0].key;
    a.confirm_open = 1;
    a.confirm_subtree = 0;
    a.confirm_victims = victims;
    a.confirm_count = 1;

    TT_EQ_INT(frame_init(&f, 65536), 0);
    render_all(&f, &a, 120, 30, 0);
    f.buf[f.len] = L'\0';

    TT_CHECK(wcsstr(f.buf, L"CPU") == NULL);           /* meters absent */
    TT_CHECK(wcsstr(f.buf, L"quit") == NULL);           /* key bar absent */
    TT_CHECK(wcsstr(f.buf, L"procs") == NULL);          /* header absent */
    TT_CHECK(wcsstr(f.buf, L"Terminate? [y/N]") != NULL);
    TT_CHECK(wcsstr(f.buf, L"zzztargetproc.exe") != NULL);

    /* Home-and-erase still opens the frame even in overlay mode. */
    TT_CHECK(wcsncmp(f.buf, L"\x1b[H\x1b[2J", 7) == 0);

    a.confirm_victims = NULL;      /* stack array - do not free() it */
    frame_free(&f);
    table_free(&a.cur);
    table_free(&a.prev);
}

/*
** MANDATORY per the brief: "a victim whose key no longer resolves is
** skipped, not killed by PID." This is the portable half of that
** property - the actual kill-time identity guard lives in plat_kill()
** and cannot be unit-tested (see the brief), but render_all() re-
** resolving every confirmed key against the LIVE a->cur, every frame, is
** what keeps the dialog from ever claiming a process is "about to die"
** once it no longer exists. Proven by rendering the SAME confirm state
** twice: once while the victim is still in a->cur (its image name shows
** up), and once after table_clear() removes it (the same key now
** resolves to nothing, so the image name is gone) - a render_all that
** cached the resolved pointer from the first call, or that resolved by
** stale row index instead of by key, would still show it the second
** time.
*/
static void test_render_all_confirm_resolves_victim_against_live_table(void)
{
    t_app       a;
    t_process   p;
    t_frame     f;
    t_proc_key  victims[1];

    mk_app(&a);
    TT_EQ_INT(table_init(&a.cur, 8), 0);
    TT_EQ_INT(table_init(&a.prev, 8), 0);
    view_init(&a.view);
    p = mk_proc(100, 1000, 4, L"livevictim.exe");
    table_add(&a.cur, &p);
    tree_build(&a.cur);

    victims[0] = a.cur.procs[0].key;
    a.confirm_open = 1;
    a.confirm_subtree = 0;
    a.confirm_victims = victims;
    a.confirm_count = 1;

    TT_EQ_INT(frame_init(&f, 65536), 0);
    render_all(&f, &a, 120, 30, 0);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"livevictim.exe") != NULL);
    frame_free(&f);

    /* The confirmed process is gone from the live table now - same key,
       nothing to resolve it to. */
    table_clear(&a.cur);

    TT_EQ_INT(frame_init(&f, 65536), 0);
    render_all(&f, &a, 120, 30, 0);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"livevictim.exe") == NULL);
    TT_CHECK(wcsstr(f.buf, L"Terminate? [y/N]") != NULL);   /* box still up */
    frame_free(&f);

    a.confirm_victims = NULL;
    table_free(&a.cur);
    table_free(&a.prev);
}

/*                       RENDER_ALL HELP OVERLAY                              */

/*
** Code review finding: draw_help() had no call site anywhere in the
** codebase - a->help_open toggled correctly (Task 19, unit-tested) and
** draw_help() itself rendered correctly (Task 18, unit-tested), but
** nothing ever connected the two, so pressing F1 in the running tool did
** nothing observable at all. Wired the same way the confirm overlay
** above already proves out: render_all() must show ONLY the help box
** while a->help_open, displacing header/meters/table/footer entirely -
** proven the same way test_render_all_confirm_open_shows_overlay_not_normal_content
** proves it for the confirm overlay, by checking a marker unique to each
** displaced section is absent while a help-specific marker is present.
*/
static void test_render_all_help_open_shows_overlay_not_normal_content(void)
{
    t_app       a;
    t_process   p;
    t_frame     f;

    mk_app(&a);
    TT_EQ_INT(table_init(&a.cur, 8), 0);
    TT_EQ_INT(table_init(&a.prev, 8), 0);
    view_init(&a.view);
    p = mk_proc(100, 1000, 4, L"zzztargetproc.exe");
    table_add(&a.cur, &p);
    tree_build(&a.cur);
    a.sys.core_count = 4;
    a.help_open = 1;

    TT_EQ_INT(frame_init(&f, 65536), 0);
    render_all(&f, &a, 120, 30, 0);
    f.buf[f.len] = L'\0';

    TT_CHECK(wcsstr(f.buf, L"CPU") == NULL);              /* meters absent */
    TT_CHECK(wcsstr(f.buf, L"zzztargetproc.exe") == NULL);/* table absent */
    TT_CHECK(wcsstr(f.buf, L"procs") == NULL);             /* header absent */
    TT_CHECK(wcsstr(f.buf, L"move selection") != NULL);    /* help content */
    TT_CHECK(wcsstr(f.buf, L"press any key to close") != NULL);
    TT_CHECK(wcsncmp(f.buf, L"\x1b[H\x1b[2J", 7) == 0);

    frame_free(&f);
    table_free(&a.cur);
    table_free(&a.prev);
}

/*
** Precedence: a confirmation open on a destructive action must win over
** the (non-modal) help overlay if both flags are somehow set at once -
** see render_all()'s own comment on why. Both set here by hand (the real
** keys_handle() dispatch never reaches this state itself, since
** confirm_open's own check runs first and swallows F1 along with every
** other key while a dialog is open - this test is exercising render_all()'s
** own defensive ordering directly, independent of how state gets there).
*/
static void test_render_all_confirm_wins_over_help_when_both_open(void)
{
    t_app       a;
    t_process   p;
    t_frame     f;
    t_proc_key  victims[1];

    mk_app(&a);
    TT_EQ_INT(table_init(&a.cur, 8), 0);
    TT_EQ_INT(table_init(&a.prev, 8), 0);
    view_init(&a.view);
    p = mk_proc(100, 1000, 4, L"victim.exe");
    table_add(&a.cur, &p);
    tree_build(&a.cur);

    victims[0] = a.cur.procs[0].key;
    a.confirm_open = 1;
    a.confirm_subtree = 0;
    a.confirm_victims = victims;
    a.confirm_count = 1;
    a.help_open = 1;

    TT_EQ_INT(frame_init(&f, 65536), 0);
    render_all(&f, &a, 120, 30, 0);
    f.buf[f.len] = L'\0';

    TT_CHECK(wcsstr(f.buf, L"Terminate? [y/N]") != NULL);
    TT_CHECK(wcsstr(f.buf, L"press any key to close") == NULL);
    TT_CHECK(wcsstr(f.buf, L"move selection") == NULL);

    a.confirm_victims = NULL;
    frame_free(&f);
    table_free(&a.cur);
    table_free(&a.prev);
}

void    test_chrome(void)
{
    test_header_width_exact_at_every_col();
    test_header_limited_marker_and_count();
    test_header_shows_ratio_when_narrowed();
    test_footer_width_exact_at_every_col();
    test_footer_filter_vs_keybar();
    test_footer_shows_kill_status();
    test_footer_kill_status_vs_filter_precedence();
    test_footer_kill_status_width_exact_at_every_col();
    test_help_width_exact_at_every_col();
    test_help_lists_every_binding();
    test_help_degrades_when_rows_too_small();
    test_help_degrades_when_cols_too_small();
    test_help_refuses_below_minimum();
    test_confirm_width_exact_at_every_col();
    test_confirm_lists_title_and_every_victim();
    test_confirm_degrades_with_more_indicator_when_rows_small();
    test_confirm_shows_title_and_prompt_with_zero_room_for_victims();
    test_confirm_refuses_below_minimum();
    test_confirm_zero_victims_is_safe();
    test_confirm_long_title_renders_in_full_on_wide_terminal();
    test_confirm_title_longer_than_any_buffer_still_ellipsizes();
    test_render_all_order_and_line_budget();
    test_render_all_first_line_width();
    test_render_all_rows_never_exceed_budget();
    test_render_all_selection_no_scroll_near_top();
    test_render_all_selection_centers_in_middle();
    test_render_all_selection_clamped_full_last_page();
    test_render_all_selection_absent_key_no_crash();
    test_render_all_confirm_open_shows_overlay_not_normal_content();
    test_render_all_confirm_resolves_victim_against_live_table();
    test_render_all_help_open_shows_overlay_not_normal_content();
    test_render_all_confirm_wins_over_help_when_both_open();
}
