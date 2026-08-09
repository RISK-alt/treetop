/* tests/test_draw_table.c */
#include "harness.h"
#include "render.h"
#include "theme.h"

#include <string.h>
#include <wchar.h>

/*
** Splits a NUL-terminated frame buffer into its "\r\n"-terminated lines
** (draw_table follows draw_meters' convention: every row ends "\r\n", the
** trailing empty line after the last one is skipped) and hands each line
** to the caller's checker. visible_len (harness.h, defined once in
** test_frame.c) is reused rather than re-implemented here, per the brief.
*/
static void for_each_line(const wchar_t *text,
                          void (*fn)(const wchar_t *line, void *ctx),
                          void *ctx)
{
    wchar_t buf[16384];
    wchar_t *line;
    wchar_t *tok_ctx;

    wcsncpy(buf, text, 16383);
    buf[16383] = L'\0';
    line = wcstok(buf, L"\n", &tok_ctx);
    while (line != NULL)
    {
        fn(line, ctx);
        line = wcstok(NULL, L"\n", &tok_ctx);
    }
}

static void check_width_cb(const wchar_t *line, void *ctx)
{
    int cols = *(int *)ctx;

    TT_CHECK((int)visible_len(line) == cols);
}

/*
** Produces an escape-free copy so position-based assertions (gutter
** offset, indent step) can index by VISIBLE column rather than raw
** buffer offset - CPU% and the orphan gutter both carry SGR codes ahead
** of the positions these tests inspect, so raw indexing would silently
** target the wrong character whenever colour is on. Same skip rule as
** visible_len (harness.h), applied to produce text instead of a count -
** a distinct utility, not a second copy of visible_len itself.
*/
static void strip_visible(const wchar_t *in, wchar_t *out, size_t cap)
{
    size_t  o;

    o = 0;
    while (*in != L'\0' && o + 1 < cap)
    {
        if (*in == L'\x1b' && in[1] == L'[')
        {
            in += 2;
            while (*in != L'\0' && *in != L'm')
                in++;
            if (*in == L'm')
                in++;
        }
        else if (*in == L'\r')
            in++;
        else
            out[o++] = *in++;
    }
    out[o] = L'\0';
}

/*
** Every emitted line must be EXACTLY cols visible columns wide - not less
** (a ragged selection bar) and not more (frame corruption from wrapping).
** Swept across the full width spread the brief calls out, including the
** pathological 1 and 0, with a row set that exercises every column: a
** tree with a following-sibling row, a last-child row, an orphan, a
** collapsed agent root and ports on more than one row.
*/
static void test_width_exact_at_every_col(void)
{
    t_process   root;
    t_process   child_a;
    t_process   child_b;
    t_process   orphan;
    t_process   *rows[4];
    t_frame     f;
    int         widths[9];
    int         i;

    root = mk_proc(100, 1000, 4, L"claude.exe");
    root.cmdline = L"claude --dangerously-skip-permissions";
    root.is_agent_root = 1;
    root.subtree_count = 5;
    root.depth = 0;

    child_a = mk_proc(200, 2000, 100, L"node.exe");
    child_a.cmdline = L"node server.js --port 3000";
    child_a.port_count = 2;
    child_a.ports[0] = 3000;
    child_a.ports[1] = 8080;
    child_a.depth = 1;

    child_b = mk_proc(300, 3000, 100, L"rg.exe");
    child_b.cmdline = NULL;
    child_b.depth = 1;

    orphan = mk_proc(400, 4000, 999, L"python.exe");
    orphan.cmdline = L"python worker.py";
    orphan.is_orphan = 1;
    orphan.depth = 0;

    rows[0] = &root;
    rows[1] = &child_a;
    rows[2] = &child_b;
    rows[3] = &orphan;

    widths[0] = 120;
    widths[1] = 100;
    widths[2] = 79;
    widths[3] = 60;
    widths[4] = 40;
    widths[5] = 20;
    widths[6] = 10;
    widths[7] = 1;
    widths[8] = 0;

    i = 0;
    while (i < 9)
    {
        t_view v;

        view_init(&v);
        TT_EQ_INT(frame_init(&f, 4096), 0);
        draw_table(&f, rows, 4, (size_t)-1, 0, widths[i], 4, &v);
        f.buf[f.len] = L'\0';
        for_each_line(f.buf, check_width_cb, &widths[i]);
        frame_free(&f);
        i++;
    }
}

/*
** Below 80 columns PORTS must merge into COMMAND as a ":3000" prefix;
** at 80 and above it is its own left-aligned column and no such prefix
** appears in the command text. A regression that always merges (or never
** merges) is caught by checking BOTH sides of the boundary.
*/
static void test_ports_merge_below_80(void)
{
    t_process   p;
    t_process   *rows[1];
    t_frame     f;
    t_view      v;

    p = mk_proc(500, 5000, 4, L"node.exe");
    p.cmdline = L"node server.js";
    p.port_count = 1;
    p.ports[0] = 3000;
    rows[0] = &p;
    view_init(&v);

    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_table(&f, rows, 1, (size_t)-1, 0, 79, 1, &v);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L":3000") != NULL);
    frame_free(&f);

    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_table(&f, rows, 1, (size_t)-1, 0, 80, 1, &v);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L":3000") == NULL);
    /* The standalone column still carries the port number. */
    TT_CHECK(wcsstr(f.buf, L"3000") != NULL);
    frame_free(&f);

    /*
    ** Multiple ports join with a comma in the standalone column - using
    ** short port numbers here (not the 3000/8080 above) because the
    ** joined text must actually fit the 8-wide column to prove the join
    ** format itself, rather than exercising the width-bound truncation
    ** test_long_ports_list_bounded already covers.
    */
    p.port_count = 2;
    p.ports[0] = 80;
    p.ports[1] = 443;
    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_table(&f, rows, 1, (size_t)-1, 0, 80, 1, &v);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"80,443") != NULL);
    frame_free(&f);
}

/*
** The selected row must be wrapped in TT_INVERT AND padded to cols WHILE
** still inside the invert/reset pair - not padded with plain trailing
** spaces after the reset closes, which would leave a ragged highlight
** bar even though the line's total visible width still happened to equal
** cols. This finds the text strictly between the invert opener and its
** matching reset and measures ITS visible width, which is the only check
** that can tell "full bar" apart from "short bar, then plain padding".
*/
static void test_selected_row_inverted_and_padded(void)
{
    t_process   a;
    t_process   b;
    t_process   *rows[2];
    t_frame     f;
    t_view      v;
    wchar_t     *inv;
    wchar_t     *rst;
    wchar_t     saved;

    a = mk_proc(100, 1000, 4, L"a.exe");
    a.cmdline = L"a";
    b = mk_proc(200, 2000, 4, L"b.exe");
    b.cmdline = L"b";
    rows[0] = &a;
    rows[1] = &b;
    view_init(&v);

    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_table(&f, rows, 2, 1, 0, 60, 2, &v);
    f.buf[f.len] = L'\0';

    /* Row 0 (not selected) carries no invert at all. */
    inv = wcsstr(f.buf, L"\x1b[7m");
    TT_CHECK(inv != NULL);

    rst = wcsstr(inv, L"\x1b[0m");
    TT_CHECK(rst != NULL);
    if (rst != NULL)
    {
        saved = *rst;
        *rst = L'\0';
        TT_EQ_INT((int)visible_len(inv + 4), 60);
        *rst = saved;
    }
    frame_free(&f);
}

static void test_collapse_arrow_direction(void)
{
    t_process   p;
    t_process   *rows[1];
    t_frame     f;
    t_view      v;

    p = mk_proc(100, 1000, 4, L"claude.exe");
    p.cmdline = L"claude";
    p.is_agent_root = 1;
    view_init(&v);

    p.collapsed = 0;
    TT_EQ_INT(frame_init(&f, 4096), 0);
    rows[0] = &p;
    draw_table(&f, rows, 1, (size_t)-1, 0, 60, 1, &v);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcschr(f.buf, L'\u25bc') != NULL);
    TT_CHECK(wcschr(f.buf, L'\u25b6') == NULL);
    frame_free(&f);

    p.collapsed = 1;
    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_table(&f, rows, 1, (size_t)-1, 0, 60, 1, &v);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcschr(f.buf, L'\u25b6') != NULL);
    TT_CHECK(wcschr(f.buf, L'\u25bc') == NULL);
    frame_free(&f);
}

/*
** Agent session roots carry theme.h's accent hue on their command text -
** the one place besides the load ramp and the orphan alarm colour that
** the palette names an intentional exception to "colour encodes load and
** nothing else". A non-root row of the same shape gets no such escape,
** so a regression that accents every row (or none) is caught either way.
*/
static void test_agent_root_accent(void)
{
    t_process   root;
    t_process   plain;
    t_process   *rows[1];
    t_frame     f;
    t_view      v;

    root = mk_proc(100, 1000, 4, L"claude.exe");
    root.cmdline = L"claude";
    root.is_agent_root = 1;

    plain = mk_proc(200, 2000, 4, L"bash.exe");
    plain.cmdline = L"bash";

    view_init(&v);

    TT_EQ_INT(frame_init(&f, 4096), 0);
    rows[0] = &root;
    draw_table(&f, rows, 1, (size_t)-1, 0, 60, 1, &v);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, TT_ACCENT) != NULL);
    frame_free(&f);

    TT_EQ_INT(frame_init(&f, 4096), 0);
    rows[0] = &plain;
    draw_table(&f, rows, 1, (size_t)-1, 0, 60, 1, &v);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, TT_ACCENT) == NULL);
    frame_free(&f);
}

/*
** The orphan '!' sits in the shared gutter, and everything after it -
** the tree prefix, the command text - must land at exactly the same
** offset as a non-orphan row's. Comparing full lines catches a shift
** either direction: a missing gutter for non-orphans would shift THEIR
** content left, not the orphan's.
*/
static void test_orphan_marker_no_column_shift(void)
{
    t_process   orphan;
    t_process   normal;
    t_process   *rows[1];
    t_frame     f;
    t_view      v;
    wchar_t     line_orphan[256];
    wchar_t     line_normal[256];
    size_t      gutter_off;

    orphan = mk_proc(100, 1000, 4, L"python.exe");
    orphan.cmdline = L"python worker.py";
    orphan.is_orphan = 1;

    normal = mk_proc(100, 1000, 4, L"python.exe");
    normal.cmdline = L"python worker.py";
    normal.is_orphan = 0;

    view_init(&v);
    gutter_off = 7 + 1 + 6 + 1 + 6 + 1 + 8 + 1; /* PID CPU MEM PORTS seps */

    TT_EQ_INT(frame_init(&f, 4096), 0);
    rows[0] = &orphan;
    draw_table(&f, rows, 1, (size_t)-1, 0, 100, 1, &v);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, TT_ORPHAN) != NULL);
    strip_visible(f.buf, line_orphan, 256);
    frame_free(&f);

    TT_EQ_INT(frame_init(&f, 4096), 0);
    rows[0] = &normal;
    draw_table(&f, rows, 1, (size_t)-1, 0, 100, 1, &v);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, TT_ORPHAN) == NULL);
    strip_visible(f.buf, line_normal, 256);
    frame_free(&f);

    TT_EQ_INT(line_orphan[gutter_off] == L'!', 1);
    TT_EQ_INT(line_normal[gutter_off] == L' ', 1);
    /* Everything from just past the gutter onward is identical. */
    TT_CHECK(wcscmp(line_orphan + gutter_off + 1,
                     line_normal + gutter_off + 1) == 0);
}

/*
** Two spaces per depth level, checked at three consecutive levels off a
** single render call so an off-by-one in the multiplier (e.g. one space
** per level, or a constant offset instead of a multiple) is caught, not
** just "indentation exists".
*/
static void test_depth_indent_step(void)
{
    t_process   d0;
    t_process   d1;
    t_process   d2;
    t_process   *rows[3];
    t_frame     f;
    t_view      v;
    wchar_t     buf[4096];
    wchar_t     *line;
    wchar_t     *ctx;
    size_t      gutter_off;

    d0 = mk_proc(100, 1000, 4, L"a.exe"); d0.cmdline = L"a"; d0.depth = 0;
    d1 = mk_proc(200, 2000, 4, L"b.exe"); d1.cmdline = L"b"; d1.depth = 1;
    d2 = mk_proc(300, 3000, 4, L"c.exe"); d2.cmdline = L"c"; d2.depth = 2;
    rows[0] = &d0;
    rows[1] = &d1;
    rows[2] = &d2;
    view_init(&v);

    gutter_off = 7 + 1 + 6 + 1 + 6 + 1 + 8 + 1;

    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_table(&f, rows, 3, (size_t)-1, 0, 100, 3, &v);
    f.buf[f.len] = L'\0';
    strip_visible(f.buf, buf, 4096);
    frame_free(&f);

    line = wcstok(buf, L"\n", &ctx);
    TT_CHECK(line != NULL);
    if (line != NULL)
        TT_CHECK(line[gutter_off + 1] == L'\u2514');   /* depth 0: 0 spaces */

    line = wcstok(NULL, L"\n", &ctx);
    TT_CHECK(line != NULL);
    if (line != NULL)
    {
        TT_CHECK(line[gutter_off + 1] == L' ');
        TT_CHECK(line[gutter_off + 2] == L' ');
        TT_CHECK(line[gutter_off + 3] == L'\u2514');   /* depth 1: 2 spaces */
    }

    line = wcstok(NULL, L"\n", &ctx);
    TT_CHECK(line != NULL);
    if (line != NULL)
    {
        TT_CHECK(line[gutter_off + 1] == L' ');
        TT_CHECK(line[gutter_off + 2] == L' ');
        TT_CHECK(line[gutter_off + 3] == L' ');
        TT_CHECK(line[gutter_off + 4] == L' ');
        TT_CHECK(line[gutter_off + 5] == L'\u2514');   /* depth 2: 4 spaces */
    }
}

/*
** A NULL cmdline falls back to the image name plus a dim em-dash; a
** non-NULL cmdline never shows that dash at all. Both directions are
** checked so a regression that always (or never) appends the dash is
** caught either way.
*/
static void test_null_cmdline_fallback(void)
{
    t_process   p;
    t_process   *rows[1];
    t_frame     f;
    t_view      v;

    p = mk_proc(100, 1000, 4, L"chrome.exe");
    p.cmdline = NULL;
    rows[0] = &p;
    view_init(&v);

    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_table(&f, rows, 1, (size_t)-1, 0, 100, 1, &v);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"chrome.exe") != NULL);
    TT_CHECK(wcschr(f.buf, L'\u2014') != NULL);
    frame_free(&f);

    p.cmdline = L"chrome.exe --flag";
    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_table(&f, rows, 1, (size_t)-1, 0, 100, 1, &v);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"chrome.exe --flag") != NULL);
    TT_CHECK(wcschr(f.buf, L'\u2014') == NULL);
    frame_free(&f);
}

/*
** A collapsed agent root appends "(N children)" with N = subtree_count -
** 1, built off a REAL tree (table_add + tree_build) so the aggregate
** subtree_count is genuinely computed, not hand-set to whatever the test
** wants to see.
*/
static void test_collapsed_agent_root_children_count(void)
{
    t_table     t;
    t_process   p;
    t_process   *root;
    t_process   *rows[1];
    t_frame     f;
    t_view      v;

    table_init(&t, 8);
    p = mk_proc(100, 1000, 4, L"claude.exe"); table_add(&t, &p);
    p = mk_proc(200, 2000, 100, L"node.exe"); table_add(&t, &p);
    p = mk_proc(300, 3000, 100, L"rg.exe");   table_add(&t, &p);
    tree_build(&t);

    root = table_find_pid(&t, 100);
    root->is_agent_root = 1;
    root->collapsed = 1;
    TT_EQ_INT(root->subtree_count, 3);

    rows[0] = root;
    view_init(&v);
    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_table(&f, rows, 1, (size_t)-1, 0, 100, 1, &v);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"(2 children)") != NULL);
    frame_free(&f);
    table_free(&t);
}

/*
** top and rows_avail must genuinely window the array: drawing a single
** row starting at top=1 out of 3 must show ONLY the middle row. A test
** that always passes top=0 (drawing everything) could never catch an
** off-by-one in the window bounds.
*/
static void test_scroll_window(void)
{
    t_process   a;
    t_process   b;
    t_process   c;
    t_process   *rows[3];
    t_frame     f;
    t_view      v;

    a = mk_proc(100, 1000, 4, L"a.exe"); a.cmdline = L"aaa";
    b = mk_proc(200, 2000, 4, L"b.exe"); b.cmdline = L"bbb";
    c = mk_proc(300, 3000, 4, L"c.exe"); c.cmdline = L"ccc";
    rows[0] = &a;
    rows[1] = &b;
    rows[2] = &c;
    view_init(&v);

    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_table(&f, rows, 3, (size_t)-1, 1, 100, 1, &v);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"bbb") != NULL);
    TT_CHECK(wcsstr(f.buf, L"aaa") == NULL);
    TT_CHECK(wcsstr(f.buf, L"ccc") == NULL);
    frame_free(&f);
}

/*
** In flat mode (v->tree_mode == 0) no tree connector is drawn at all,
** even though every row still carries a real depth field from the last
** time tree mode ran - view_flatten's flat path zeroes depth, but this
** guards the render side too, since draw_table is handed whatever depth
** happens to be on the row.
*/
static void test_flat_mode_suppresses_tree_glyphs(void)
{
    t_process   p;
    t_process   *rows[1];
    t_frame     f;
    t_view      v;

    p = mk_proc(100, 1000, 4, L"a.exe");
    p.cmdline = L"aaa";
    p.depth = 2;
    rows[0] = &p;
    view_init(&v);
    v.tree_mode = 0;

    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_table(&f, rows, 1, (size_t)-1, 0, 100, 1, &v);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcschr(f.buf, L'\u2514') == NULL);
    TT_CHECK(wcschr(f.buf, L'\u251c') == NULL);
    frame_free(&f);
}

/*
** A pathologically long ports list must not push the row past cols: the
** PORTS column is hard-bounded to its 8-character width even when the
** joined "port,port,port..." text would otherwise be much longer.
*/
static void test_long_ports_list_bounded(void)
{
    t_process   p;
    t_process   *rows[1];
    t_frame     f;
    t_view      v;
    int         i;

    p = mk_proc(100, 1000, 4, L"a.exe");
    p.cmdline = L"aaa";
    p.port_count = TT_MAX_PORTS;
    i = 0;
    while (i < TT_MAX_PORTS)
    {
        p.ports[i] = (unsigned short)(10000 + i);
        i++;
    }
    rows[0] = &p;
    view_init(&v);

    TT_EQ_INT(frame_init(&f, 4096), 0);
    draw_table(&f, rows, 1, (size_t)-1, 0, 100, 1, &v);
    f.buf[f.len] = L'\0';
    {
        int cols = 100;

        for_each_line(f.buf, check_width_cb, &cols);
    }
    frame_free(&f);
}

void    test_draw_table(void)
{
    test_width_exact_at_every_col();
    test_ports_merge_below_80();
    test_selected_row_inverted_and_padded();
    test_collapse_arrow_direction();
    test_agent_root_accent();
    test_orphan_marker_no_column_shift();
    test_depth_indent_step();
    test_null_cmdline_fallback();
    test_collapsed_agent_root_children_count();
    test_scroll_window();
    test_flat_mode_suppresses_tree_glyphs();
    test_long_ports_list_bounded();
}
