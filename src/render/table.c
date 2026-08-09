#include "render.h"
#include "theme.h"

#include <wchar.h>

/*
** This file is portable C - no Win32 - so it lives in treetop_core and
** treetop_tests can exercise it against synthetic t_process rows, no
** live console or process table required.
**
** Column order is PID, CPU%, MEM, [PORTS], COMMAND - the last one
** "remainder" in the brief's own words. The gutter (orphan '!' / the
** expand arrow) and the tree indent live INSIDE the COMMAND cell, first
** thing in it: "The COMMAND cell is built as: gutter, tree prefix, then
** the command line." Everything left of COMMAND is a fixed-width numeric
** column so figures never jitter frame to frame; COMMAND alone absorbs
** whatever width is left, including none at all on a very narrow
** terminal.
**
** Every function below tracks a running `avail` (visible columns still
** free on the line) and only ever writes content that fits inside it,
** rather than writing first and truncating after - that is what makes
** "every emitted line is exactly cols wide" hold even at the
** pathological widths (10, 1, 0) the test suite sweeps, with no
** possibility of a color escape being cut in half.
*/

#define TT_CORE_BUF     512

/*                                    SEL                                     */

/*
** The selected row is wrapped in TT_INVERT/TT_RESET around the WHOLE
** padded line, per the brief. SGR reset (the "0" in TT_RESET) clears
** every active attribute, including inverse video - so if any inner
** colour (theme_load's ramp, the orphan alarm, the accent hue, dim) were
** left on inside a selected row, its own closing TT_RESET would kill the
** outer invert partway through the line and the highlight would stop
** early. Every colour call below is therefore gated on `!sel`: a
** selected row is plain text throughout, made visible entirely by the
** one outer invert/reset pair.
*/

/*                                  HELPERS                                   */

/*
** Individual p->cpu_pct is already capped at 100.0 by delta_apply(), but
** p->subtree_cpu (the sum of a whole subtree's already-capped figures,
** shown here in place of a collapsed root's own - see draw_row below) can
** legitimately run well past that on a multi-process, multi-core-busy
** tree, and must still fit the fixed 6-column CPU% cell.  999.9 is the
** largest value "%5.1f" ever prints in exactly 5 characters - one more
** digit would overflow the field and break the "every line is exactly
** cols wide" invariant - so that, not 100.0, is the real display bound.
*/
static double   clamp_pct(double pct)
{
    if (pct < 0.0)
        return (0.0);
    if (pct > 999.9)
        return (999.9);
    return (pct);
}

/*
** Comma-joined port list, e.g. "80,443,3000". Bounded on cap so a
** pathological port count (up to TT_MAX_PORTS) can never overrun `out` -
** used both for the standalone PORTS column and, for a single port only,
** the narrow-terminal ":3000" merge prefix.
*/
static void format_ports(const t_process *p, wchar_t *out, size_t cap)
{
    wchar_t seg[8];
    size_t  pos;
    size_t  seglen;
    int     i;

    if (cap == 0)
        return ;
    pos = 0;
    i = 0;
    while (i < p->port_count)
    {
        if (i > 0)
        {
            if (pos + 1 >= cap)
                break ;
            out[pos++] = L',';
        }
        swprintf(seg, 8, L"%u", (unsigned int)p->ports[i]);
        seglen = wcslen(seg);
        if (pos + seglen >= cap)
            break ;
        wmemcpy(out + pos, seg, seglen);
        pos += seglen;
        i++;
    }
    out[pos] = L'\0';
}

/*
** rows is already a depth-first flattening (view_flatten): whether row i
** has a following sibling can be read straight off that order without
** any parent/next_sibling walk, by looking ahead for the next row at the
** SAME depth before one at a SHALLOWER depth turns up (which means the
** subtree closed with no sibling). This stays correct under filtering,
** since rows only ever contains what is actually visible - there is no
** hidden sibling this could get wrong by skipping past.
*/
static int  row_is_last(t_process **rows, size_t n, size_t i)
{
    int     depth;
    size_t  j;

    depth = rows[i]->depth;
    j = i + 1;
    while (j < n && rows[j]->depth > depth)
        j++;
    return (!(j < n && rows[j]->depth == depth));
}

/*                                COMMAND CELL                                */

/*
** Builds the gutter + tree prefix + shortened command text into `f`,
** never writing more than `avail` visible columns. Returns how many
** visible columns it actually used, so the caller can pad the rest of
** the line. Each optional piece (tree prefix, ports-merge prefix, the
** NULL-cmdline dash, the collapsed-count tail) is included only if it
** fits in what remains - dropped silently otherwise rather than emitted
** partially, since a half-written glyph or escape is worse than a
** slightly sparser row on an unreasonably narrow terminal.
*/
static int  draw_command_cell(t_frame *f, const t_process *p, int avail,
                              int sel, int show_ports, int show_tree,
                              int is_last)
{
    wchar_t         treebuf[64];
    wchar_t         portpfx[16];
    wchar_t         tailbuf[48];
    wchar_t         corebuf[TT_CORE_BUF];
    int             depth;
    int             tlen;
    int             plen;
    int             tail_len;
    int             dash_len;
    int             core_budget;
    int             core_len;
    int             used;
    int             i;

    if (avail <= 0)
        return (0);
    if (p->is_orphan && !sel)
    {
        frame_puts(f, TT_ORPHAN);
        frame_puts(f, L"!");
        frame_puts(f, TT_RESET);
    }
    else if (p->is_orphan)
        frame_puts(f, L"!");
    else if (p->is_agent_root)
        frame_puts(f, p->collapsed ? L"\u25b6" : L"\u25bc");
    else
        frame_puts(f, L" ");
    used = 1;
    avail -= 1;
    if (avail <= 0)
        return (used);
    tlen = 0;
    if (show_tree)
    {
        depth = p->depth;
        if (depth < 0)
            depth = 0;
        if (depth > 24)
            depth = 24;
        tlen = depth * 2 + 2;
        if (tlen <= avail)
        {
            i = 0;
            while (i < depth * 2)
                treebuf[i++] = L' ';
            treebuf[i++] = is_last ? L'\u2514' : L'\u251c';
            treebuf[i++] = L' ';
            treebuf[i] = L'\0';
            frame_puts(f, treebuf);
            avail -= tlen;
            used += tlen;
        }
    }
    if (avail <= 0)
        return (used);
    plen = 0;
    if (!show_ports && p->port_count > 0)
    {
        swprintf(portpfx, 16, L":%u ", (unsigned int)p->ports[0]);
        plen = (int)wcslen(portpfx);
        if (plen <= avail)
        {
            frame_puts(f, portpfx);
            avail -= plen;
            used += plen;
        }
    }
    tail_len = 0;
    if (p->is_agent_root && p->collapsed)
    {
        swprintf(tailbuf, 48, L" (%d children)", p->subtree_count - 1);
        tail_len = (int)wcslen(tailbuf);
    }
    dash_len = (p->cmdline == NULL) ? 2 : 0;
    if (dash_len + tail_len > avail)
    {
        tail_len = 0;
        if (dash_len > avail)
            dash_len = 0;
    }
    core_budget = avail - dash_len - tail_len;
    if (core_budget < 0)
        core_budget = 0;
    if (core_budget > TT_CORE_BUF - 1)
        core_budget = TT_CORE_BUF - 1;
    if (p->cmdline != NULL)
        fmt_command(p->image, p->cmdline, (size_t)core_budget, corebuf,
                    TT_CORE_BUF);
    else
        fmt_shorten(p->image, (size_t)core_budget, corebuf, TT_CORE_BUF);
    core_len = (int)wcslen(corebuf);
    if (!sel && p->is_agent_root)
    {
        frame_puts(f, TT_ACCENT);
        frame_puts(f, corebuf);
        frame_puts(f, TT_RESET);
    }
    else
        frame_puts(f, corebuf);
    used += core_len;
    if (dash_len > 0)
    {
        if (!sel)
        {
            frame_puts(f, TT_DIM);
            frame_puts(f, L" \u2014");
            frame_puts(f, TT_RESET);
        }
        else
            frame_puts(f, L" \u2014");
        used += dash_len;
    }
    if (tail_len > 0)
    {
        if (!sel)
        {
            frame_puts(f, TT_DIM);
            frame_puts(f, tailbuf);
            frame_puts(f, TT_RESET);
        }
        else
            frame_puts(f, tailbuf);
        used += tail_len;
    }
    return (used);
}

/*                                    ROW                                     */

/*
** PID, CPU% and MEM are shown only if the FULL fixed width they need is
** still free - never partially, which would leave a truncated digit
** string. PORTS follows the same rule but is additionally gated on
** show_ports (cols >= 80). Once one of these does not fit, everything
** after it is skipped too - COMMAND then gets whatever is left, down to
** nothing - and the line is padded out to `cols` regardless, so the
** total width invariant holds independently of how many columns a
** narrow terminal actually got to see.
**
** CPU% and MEM read p->subtree_cpu/p->subtree_mem instead of p's own
** cpu_pct/working_set whenever p->collapsed - tree_build()'s aggregate()
** pass (src/model/tree.c) already rolls a whole subtree's figures onto
** its root, but until this was wired in here nothing ever displayed them:
** collapsing a busy root hid its children AND the CPU/memory they were
** using, showing only the root's own idle-looking numbers, which is
** backwards from what collapsing a subtree view is for. An uncollapsed
** row keeps showing its own figures exactly as before - subtree_cpu/
** subtree_mem still exist on it (aggregate() computes them for every
** node, not just collapsed ones) but a row already showing its children
** individually has no reason to also fold their totals into its own.
*/
static void draw_row(t_frame *f, const t_process *p, int cols, int sel,
                     int show_ports, int show_tree, int is_last)
{
    wchar_t         buf[32];
    wchar_t         membuf[16];
    wchar_t         portbuf[64];
    double          pct;
    const wchar_t   *color;
    int             avail;
    int             content;

    if (sel)
        frame_puts(f, TT_INVERT);
    avail = cols;
    content = 0;
    if (avail >= 7)
    {
        swprintf(buf, 32, L"%7lu", p->key.pid);
        frame_puts(f, buf);
        avail -= 7;
        content += 7;
        if (avail >= 1)
        {
            frame_puts(f, L" ");
            avail -= 1;
            content += 1;
        }
    }
    if (avail >= 6)
    {
        pct = clamp_pct(p->collapsed ? p->subtree_cpu : p->cpu_pct);
        swprintf(buf, 32, L"%5.1f%%", pct);
        if (!sel)
        {
            color = theme_load(pct);
            frame_puts(f, color);
            frame_puts(f, buf);
            if (color[0] != L'\0')
                frame_puts(f, TT_RESET);
        }
        else
            frame_puts(f, buf);
        avail -= 6;
        content += 6;
        if (avail >= 1)
        {
            frame_puts(f, L" ");
            avail -= 1;
            content += 1;
        }
    }
    if (avail >= 6)
    {
        fmt_bytes(p->collapsed ? p->subtree_mem : p->working_set, membuf, 16);
        frame_printf(f, L"%6.6ls", membuf);
        avail -= 6;
        content += 6;
        if (avail >= 1)
        {
            frame_puts(f, L" ");
            avail -= 1;
            content += 1;
        }
    }
    if (show_ports && avail >= 8)
    {
        format_ports(p, portbuf, 64);
        frame_printf(f, L"%-8.8ls", portbuf);
        avail -= 8;
        content += 8;
        if (avail >= 1)
        {
            frame_puts(f, L" ");
            avail -= 1;
            content += 1;
        }
    }
    content += draw_command_cell(f, p, avail, sel, show_ports, show_tree,
                                 is_last);
    if (content < cols)
        frame_pad(f, cols - content);
    if (sel)
        frame_puts(f, TT_RESET);
}

/*                                  HEADER                                    */

/*
** One field of the column header row: `label`, with a small arrow glyph
** appended (pointing the way sort_desc does) when this is the column
** view->sort currently names, then justified into `width` cells - right
** for the three numeric columns, left for PORTS, so each lines up with
** draw_row's own alignment for that column. Bounded to a 15-character
** label plus a 1-cell arrow, comfortably above anything this file ever
** actually calls it with ("CPU%" is the longest).
*/
static void header_field(t_frame *f, const wchar_t *label, int width,
                        int right_align, int active, int desc)
{
    wchar_t text[16];
    wchar_t out[24];
    size_t  len;

    wcsncpy(text, label, 15);
    text[15] = L'\0';
    if (active)
    {
        len = wcslen(text);
        if (len + 1 < 16)
        {
            text[len] = desc ? L'\u25bc' : L'\u25b2';
            text[len + 1] = L'\0';
        }
    }
    if (right_align)
        swprintf(out, 24, L"%*ls", width, text);
    else
        swprintf(out, 24, L"%-*ls", width, text);
    frame_puts(f, out);
}

/*
** Labels every column draw_row itself draws, in the same order and at the
** same fixed widths, so header text and row data always line up - PID,
** CPU%, MEM and (above 80 columns) PORTS right where their numbers sit,
** COMMAND after the same one-column gutter draw_command_cell reserves for
** the orphan/collapse marker. The active sort column (view->sort, cycled
** by F6/</>) carries a small triangle pointing the current direction;
** PORTS is never a sort column (there is no SORT_PORTS in t_sort_mode) so
** it never carries one. Follows draw_row's own "each fixed field only if
** the FULL width still fits, then pad the remainder" discipline, so this
** obeys the same exact-cols width invariant every other rendered line
** does.
*/
void    draw_table_header(t_frame *f, int cols, t_sort_mode sort,
                        int sort_desc)
{
    int     avail;
    int     content;
    int     show_ports;
    int     cmd_len;

    avail = cols;
    content = 0;
    show_ports = (cols >= 80);
    if (avail >= 7)
    {
        header_field(f, L"PID", 7, 1, sort == SORT_PID, sort_desc);
        avail -= 7;
        content += 7;
        if (avail >= 1)
        {
            frame_puts(f, L" ");
            avail -= 1;
            content += 1;
        }
    }
    if (avail >= 6)
    {
        header_field(f, L"CPU%", 6, 1, sort == SORT_CPU, sort_desc);
        avail -= 6;
        content += 6;
        if (avail >= 1)
        {
            frame_puts(f, L" ");
            avail -= 1;
            content += 1;
        }
    }
    if (avail >= 6)
    {
        header_field(f, L"MEM", 6, 1, sort == SORT_MEM, sort_desc);
        avail -= 6;
        content += 6;
        if (avail >= 1)
        {
            frame_puts(f, L" ");
            avail -= 1;
            content += 1;
        }
    }
    if (show_ports && avail >= 8)
    {
        header_field(f, L"PORTS", 8, 0, 0, 0);
        avail -= 8;
        content += 8;
        if (avail >= 1)
        {
            frame_puts(f, L" ");
            avail -= 1;
            content += 1;
        }
    }
    if (avail >= 1)
    {
        frame_puts(f, L" ");
        avail -= 1;
        content += 1;
    }
    cmd_len = (sort == SORT_NAME) ? 8 : 7;
    if (avail >= cmd_len)
    {
        header_field(f, L"COMMAND", cmd_len, 0, sort == SORT_NAME,
                sort_desc);
        avail -= cmd_len;
        content += cmd_len;
    }
    if (content < cols)
        frame_pad(f, cols - content);
}

/*                                   TABLE                                    */

void    draw_table(t_frame *f, t_process **rows, size_t n, size_t sel,
                   int top, int cols, int rows_avail, const t_view *v)
{
    size_t  start;
    size_t  span;
    size_t  end;
    size_t  i;
    int     show_ports;

    start = (top < 0) ? 0 : (size_t)top;
    span = (rows_avail < 0) ? 0 : (size_t)rows_avail;
    end = start + span;
    if (end > n)
        end = n;
    show_ports = (cols >= 80);
    i = start;
    while (i < end)
    {
        draw_row(f, rows[i], cols, (i == sel), show_ports, v->tree_mode,
                row_is_last(rows, n, i));
        frame_puts(f, L"\r\n");
        i++;
    }
}
