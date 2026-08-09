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
            draw_header(&f, &a, widths[i], limited_vals[j]);
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
    draw_header(&f, &a, 120, 1);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"limited mode") != NULL);
    TT_CHECK(wcsstr(f.buf, L"treetop") != NULL);
    TT_CHECK(wcsstr(f.buf, L"7 procs") != NULL);
    frame_free(&f);

    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_header(&f, &a, 120, 0);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"limited mode") == NULL);
    TT_CHECK(wcsstr(f.buf, L"treetop") != NULL);
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

void    test_chrome(void)
{
    test_header_width_exact_at_every_col();
    test_header_limited_marker_and_count();
    test_footer_width_exact_at_every_col();
    test_footer_filter_vs_keybar();
    test_help_width_exact_at_every_col();
    test_help_lists_every_binding();
    test_help_degrades_when_rows_too_small();
    test_help_degrades_when_cols_too_small();
    test_help_refuses_below_minimum();
    test_render_all_order_and_line_budget();
    test_render_all_first_line_width();
}
