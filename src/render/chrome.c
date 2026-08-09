#include "render.h"
#include "theme.h"

#include <stdlib.h>
#include <time.h>
#include <wchar.h>

/*
** This file is portable C - no Win32 - so it lives in treetop_core and
** treetop_tests can exercise a whole rendered frame without a live
** console, exactly like format.c, frame.c, meters.c and table.c already
** do. draw_header/render_all take `limited` as an explicit parameter
** rather than calling plat_limited() (see render.h) or even trusting
** a->limited for the same reason: this file is never the place that
** decides which platform fact is the truth, only the place that draws
** whatever it is handed.
**
** Colour discipline follows table.c's precedent exactly: TT_DIM,
** TT_INVERT and TT_ACCENT are structural markers unrelated to load (see
** theme.h) and are emitted unconditionally, the same way table.c already
** does for the orphan alarm and the selection highlight. theme_load()
** (the load ramp) is the only colour source g_color gates, and nothing
** in this file calls it - header, footer and help carry no load figures
** of their own.
*/

/*                                   HEADER                                   */

# define TT_HDR_TITLE       L" treetop"
# define TT_HDR_LIMITED     L"  limited mode"

/*
** Title, "limited mode", process count and clock are each written only
** if they individually still fit in what remains of `cols` - and,
** unlike draw_table's fixed numeric columns, each is tried independently
** rather than the first miss aborting everything after it. There is no
** cross-row alignment to protect here (this is one line, not N rows of
** the same columns), so letting a later, shorter piece show up even when
** an earlier one did not fit is strictly more informative and no less
** correct. Whatever is left after all four attempts is spaces, so the
** line is always exactly `cols` wide by construction.
**
** "limited mode" is tried right after the title - ahead of the process
** count and the clock - because a degraded run being visible is the
** entire point of this marker (see the brief): it should survive
** truncation on a modestly narrow terminal even though the brief lists
** it last among the four things the header shows.
**
** The process count reads "N / M procs" whenever visible_count differs
** from a->cur.count - not only under a text filter, but under
** agents-only or orphans-only too, since all three narrow what
** draw_table actually shows the same way. A bare "M procs" while the
** table displays a fraction of that is exactly the kind of small
** inconsistency the brief's own review called out; matching on "does the
** displayed count differ from the total" catches every way the view can
** narrow it, not just the one named in the example.
*/
void    draw_header(t_frame *f, const t_app *a, int cols, int limited,
                    size_t visible_count)
{
    wchar_t         clockbuf[16];
    wchar_t         procsbuf[48];
    time_t          now;
    struct tm       *tmv;
    int             avail;
    int             len;

    avail = cols;
    len = (int)wcslen(TT_HDR_TITLE);
    if (avail >= len)
    {
        frame_puts(f, TT_HDR_TITLE);
        avail -= len;
    }
    if (limited)
    {
        len = (int)wcslen(TT_HDR_LIMITED);
        if (avail >= len)
        {
            frame_puts(f, TT_DIM);
            frame_puts(f, TT_HDR_LIMITED);
            frame_puts(f, TT_RESET);
            avail -= len;
        }
    }
    if (visible_count != a->cur.count)
        swprintf(procsbuf, 48, L"  %llu / %llu procs",
                (unsigned long long)visible_count,
                (unsigned long long)a->cur.count);
    else
        swprintf(procsbuf, 48, L"  %llu procs",
                (unsigned long long)a->cur.count);
    len = (int)wcslen(procsbuf);
    if (avail >= len)
    {
        frame_puts(f, procsbuf);
        avail -= len;
    }
    now = time(NULL);
    tmv = localtime(&now);
    if (tmv != NULL)
        swprintf(clockbuf, 16, L"  %02d:%02d:%02d", tmv->tm_hour,
                tmv->tm_min, tmv->tm_sec);
    else
        wcscpy(clockbuf, L"  --:--:--");
    len = (int)wcslen(clockbuf);
    if (avail >= len)
    {
        frame_puts(f, clockbuf);
        avail -= len;
    }
    frame_pad(f, avail);
    frame_puts(f, L"\r\n");
}

/*                                   FOOTER                                   */

/*
** This task predates the input state machine (Task 19): there is no
** "editing a filter" flag anywhere yet, only the live filter text itself
** (t_view::filter). The only state this file can observe is therefore
** whether that text is non-empty, which is what decides which of the two
** footer layouts below is drawn. Task 19 owns making that true exactly
** when the user intends the prompt to show - this file just renders
** whatever v->filter currently holds.
*/

/*
** Forward declaration: overlay_trunc()'s real definition lives further
** down, shared with draw_help() and draw_confirm() (see its own comment
** there). draw_footer_status() below needs it earlier in the file than
** that.
*/
static void overlay_trunc(const wchar_t *src, int max, wchar_t *out,
                size_t n);

/*
** "/" + the live filter text + a one-cell inverted-space cursor, padded
** to `cols`. fmt_shorten (not a right-truncating helper) is deliberately
** reused here: it keeps the TAIL of the source string, which is exactly
** what belongs right next to a cursor that sits at the end of whatever
** the user just typed - older characters scroll off the left behind a
** leading ellipsis, precisely fmt_shorten's own documented behaviour for
** a string whose meaning lives in the tail.
*/
static void draw_footer_filter(t_frame *f, const t_view *v, int cols)
{
    wchar_t buf[TT_FILTER_LEN + 2];
    int     avail;
    int     used;
    int     text_budget;

    avail = cols;
    used = 0;
    if (avail >= 1)
    {
        frame_puts(f, L"/");
        used = 1;
    }
    text_budget = avail - used - 1;
    if (text_budget < 0)
        text_budget = 0;
    fmt_shorten(v->filter, (size_t)text_budget, buf, TT_FILTER_LEN + 2);
    frame_puts(f, buf);
    used += (int)wcslen(buf);
    if (used < avail)
    {
        frame_puts(f, TT_INVERT);
        frame_puts(f, L" ");
        frame_puts(f, TT_RESET);
        used += 1;
    }
    if (used < avail)
        frame_pad(f, avail - used);
}

/*
** A compact subset of the bindings - the full list is the help overlay,
** not this line. Ordered by what a lost user needs most: quit and help
** are the two bindings nothing else on screen hints at, so they are
** tried first and are the ones guaranteed to survive on a narrow
** terminal; the rest follow roughly brief order. Once one segment does
** not fit, later segments are not attempted either (same rule
** draw_table's fixed columns already use) - there is no reordering to
** backfill a narrower one that might have fit, by design, for the same
** reason draw_row does not either: simple and predictable beats
** optimally packed.
*/
static const wchar_t   *g_footer_keys[] = {
    L"q quit",
    L"F1 help",
    L"/ filter",
    L"\u2191\u2193 move",
    L"\u2190\u2192 fold",
    L"F5 tree",
    L"F9 kill",
    L"p pause",
};

# define TT_FOOTER_KEY_COUNT \
    (sizeof(g_footer_keys) / sizeof(g_footer_keys[0]))

static void draw_footer_keys(t_frame *f, int cols)
{
    int avail;
    int used;
    int first;
    int seg_len;
    int need;
    int i;

    avail = cols;
    used = 0;
    first = 1;
    i = 0;
    while (i < (int)TT_FOOTER_KEY_COUNT)
    {
        seg_len = (int)wcslen(g_footer_keys[i]);
        need = seg_len + (first ? 0 : 3);
        if (avail - used < need)
            break ;
        if (!first)
        {
            frame_puts(f, TT_DIM);
            frame_puts(f, L" \u2502 ");
            frame_puts(f, TT_RESET);
        }
        frame_puts(f, g_footer_keys[i]);
        used += need;
        first = 0;
        i++;
    }
    frame_pad(f, avail - used);
}

/*
** a->kill_status (Task 20: "access denied - try running as
** administrator" / "process already exited", set by src/main.c after a
** plat_kill() failure - see app.h) right-truncates via overlay_trunc(),
** not fmt_shorten(): the identifying word ("access", "process") is the
** FIRST thing on the line here, same reasoning as help_trunc's own
** comment for the HELP key column, and the opposite of
** draw_footer_filter's cursor-adjacent tail above.
*/
static void draw_footer_status(t_frame *f, const wchar_t *msg, int cols)
{
    wchar_t buf[128];
    int     avail;
    int     used;

    avail = cols;
    if (avail < 0)
        avail = 0;
    overlay_trunc(msg, avail, buf, 128);
    frame_puts(f, buf);
    used = (int)wcslen(buf);
    if (used < avail)
        frame_pad(f, avail - used);
}

/*
** Three mutually exclusive layouts, checked in this order: an active
** filter always wins (it needs the user's own typing visible and
** editable, which nothing else on this line competes for); otherwise a
** pending kill_status message from the last F9/Shift+F9 attempt is shown
** until the next one overwrites or clears it; otherwise the ordinary key
** bar. There is no timer that expires kill_status on its own - it is
** exactly as persistent as a->paused, cleared only by the next
** open_kill_confirm() (a fresh dialog opening - see keys.c) or the next
** execute_confirmed_kill() (src/main.c) outcome.
*/
void    draw_footer(t_frame *f, const t_app *a, int cols)
{
    if (a->view.filter[0] != L'\0')
        draw_footer_filter(f, &a->view, cols);
    else if (a->kill_status[0] != L'\0')
        draw_footer_status(f, a->kill_status, cols);
    else
        draw_footer_keys(f, cols);
    frame_puts(f, L"\r\n");
}

/*                                    HELP                                    */

typedef struct s_help_entry
{
    const wchar_t   *key;
    const wchar_t   *action;
}   t_help_entry;

/*
** Every binding named in the brief, key column first. TT_HELP_KEY_COL
** (10) is sized to the longest key label here ("\u2190/\u2192/Space", 9
** visible cells) plus one separating space - wide enough that no key
** ever runs into its own action text, narrow enough not to waste budget
** that narrow-terminal truncation (see draw_help) needs for the action
** column instead.
*/
static const t_help_entry  g_help_bindings[] = {
    { L"\u2191 / \u2193",      L"move selection" },
    { L"\u2190/\u2192/Space",  L"collapse / expand" },
    { L"a",                    L"agents-only view" },
    { L"o",                    L"orphans-only view" },
    { L"/",                    L"filter" },
    { L"Esc",                  L"clear filter" },
    { L"F5",                   L"tree / flat view" },
    { L"F6, <, >",              L"sort column" },
    { L"F9",                   L"kill process" },
    { L"Shift+F9",              L"kill subtree" },
    { L"p",                    L"pause" },
    { L"+ / -",                 L"refresh interval" },
    { L"F1, ?",                 L"help" },
    { L"q",                    L"quit" },
};

# define TT_HELP_KEY_COL        10
# define TT_HELP_BIND_COUNT \
    (sizeof(g_help_bindings) / sizeof(g_help_bindings[0]))
/* title line + one per binding + the dismiss hint. */
# define TT_HELP_CONTENT_MAX    (TT_HELP_BIND_COUNT + 2)
# define TT_HELP_LINE_BUF       48

/*
** Formats every content line (title, one per binding, dismiss hint) at
** its natural, untruncated width. draw_help decides afterwards how much
** of this actually fits the terminal it was given.
*/
static size_t   build_help_lines(wchar_t lines[][TT_HELP_LINE_BUF])
{
    size_t  i;

    swprintf(lines[0], TT_HELP_LINE_BUF, L"treetop \u2014 help");
    i = 0;
    while (i < TT_HELP_BIND_COUNT)
    {
        swprintf(lines[i + 1], TT_HELP_LINE_BUF, L"%-*ls%ls",
                TT_HELP_KEY_COL, g_help_bindings[i].key,
                g_help_bindings[i].action);
        i++;
    }
    swprintf(lines[TT_HELP_BIND_COUNT + 1], TT_HELP_LINE_BUF,
            L"press any key to close");
    return (TT_HELP_BIND_COUNT + 2);
}

/*
** Right-truncates with a trailing ellipsis, the mirror image of
** fmt_shorten's leading one: a help line's or a kill-confirm victim
** line's identifying token (the key, or the PID) is the FIRST thing on
** it, so the tail is what gets cut, the same reasoning fmt_command's own
** comment gives for the COMMAND column. This is not fmt_command itself -
** there is no image/cmdline split here, just one already-composed
** string - so a small local helper is genuinely a distinct case, not a
** second copy of an existing one. Mirrors fmt_shorten's own
** degenerate-width guard (bounded to n, not just max - see commit
** cde8307) for the same reason: a caller handing this a buffer of
** exactly one wchar_t must still get a valid, in-bounds result.
**
** Shared, along with overlay_blank_rows()/overlay_border_row() below, by
** every box-shaped overlay this file draws - draw_help() (Task 18) and
** draw_confirm() (Task 20) - since both degrade the same way: shrink the
** box to fit cols, truncate content lines that do not fit, and refuse to
** draw a malformed box at all below a minimum viable size.
*/
static void overlay_trunc(const wchar_t *src, int max, wchar_t *out, size_t n)
{
    size_t  len;

    if (n == 0)
        return ;
    if (max <= 0)
    {
        out[0] = L'\0';
        return ;
    }
    len = wcslen(src);
    if ((int)len <= max)
    {
        swprintf(out, n, L"%ls", src);
        return ;
    }
    if (max < 2)
    {
        if (n >= 2)
        {
            out[0] = L'\u2026';
            out[1] = L'\0';
        }
        else
            out[0] = L'\0';
        return ;
    }
    swprintf(out, n, L"%.*ls\u2026", max - 1, src);
}

static void overlay_blank_rows(t_frame *f, int cols, int count)
{
    int i;

    i = 0;
    while (i < count)
    {
        frame_pad(f, cols);
        frame_puts(f, L"\r\n");
        i++;
    }
}

static void overlay_border_row(t_frame *f, int left_pad, int right_pad,
                            int inner_w, int top)
{
    int i;

    frame_pad(f, left_pad);
    frame_puts(f, top ? L"\u250c" : L"\u2514");
    i = 0;
    while (i < inner_w + 2)
    {
        frame_puts(f, L"\u2500");
        i++;
    }
    frame_puts(f, top ? L"\u2510" : L"\u2518");
    frame_pad(f, right_pad);
    frame_puts(f, L"\r\n");
}

/*
** Centred, multi-line overlay listing every binding. Degrades on two
** independent axes, each a deliberate decision this file's own tests
** enforce (see test_chrome.c):
**
**   - cols smaller than the box's natural width: the box itself shrinks
**     to fit, and every content line is right-truncated with a trailing
**     ellipsis (overlay_trunc) rather than wrapped.
**   - rows smaller than the box's natural height: rows are dropped from
**     the BOTTOM of the content list (title first, then bindings in
**     brief order, the dismiss hint last) rather than every line being
**     squeezed or the box refusing to draw.
**   - below a minimum viable size on EITHER axis (cols < 5: no room for
**     even one truncated character between the borders; rows < 3: no
**     room for a single content row between top and bottom borders) the
**     overlay refuses to draw a box at all and emits `rows` blank lines
**     of `cols` spaces instead - a malformed box is worse than no box.
**
** Every one of the `rows` lines emitted, in every branch, is exactly
** `cols` visible columns wide by construction: the blank-only path pads
** every line to cols directly, and the boxed path sizes left_pad/right_pad
** and top_blank/bottom_blank so the totals land on cols and rows exactly
** (see the arithmetic below).
*/
void    draw_help(t_frame *f, int cols, int rows)
{
    wchar_t     lines[TT_HELP_CONTENT_MAX][TT_HELP_LINE_BUF];
    wchar_t     trunc_buf[TT_HELP_LINE_BUF];
    size_t      content_count;
    int         inner_desired;
    int         box_w;
    int         box_h;
    int         inner_w;
    int         content_cap;
    int         content_shown;
    int         left_pad;
    int         right_pad;
    int         top_blank;
    int         bottom_blank;
    int         i;

    content_count = build_help_lines(lines);
    inner_desired = 0;
    i = 0;
    while ((size_t)i < content_count)
    {
        if ((int)wcslen(lines[i]) > inner_desired)
            inner_desired = (int)wcslen(lines[i]);
        i++;
    }
    box_w = cols;
    if (box_w > inner_desired + 4)
        box_w = inner_desired + 4;
    box_h = rows;
    if (box_h > (int)content_count + 2)
        box_h = (int)content_count + 2;
    if (box_w < 5 || box_h < 3)
    {
        overlay_blank_rows(f, cols, rows);
        return ;
    }
    inner_w = box_w - 4;
    content_cap = box_h - 2;
    content_shown = (int)content_count;
    if (content_shown > content_cap)
        content_shown = content_cap;
    left_pad = (cols - box_w) / 2;
    right_pad = cols - box_w - left_pad;
    top_blank = (rows - box_h) / 2;
    bottom_blank = rows - box_h - top_blank;
    overlay_blank_rows(f, cols, top_blank);
    overlay_border_row(f, left_pad, right_pad, inner_w, 1);
    i = 0;
    while (i < content_cap)
    {
        frame_pad(f, left_pad);
        frame_puts(f, L"\u2502 ");
        if (i < content_shown)
        {
            overlay_trunc(lines[i], inner_w, trunc_buf, TT_HELP_LINE_BUF);
            frame_printf(f, L"%-*ls", inner_w, trunc_buf);
        }
        else
            frame_pad(f, inner_w);
        frame_puts(f, L" \u2502");
        frame_pad(f, right_pad);
        frame_puts(f, L"\r\n");
        i++;
    }
    overlay_border_row(f, left_pad, right_pad, inner_w, 0);
    overlay_blank_rows(f, cols, bottom_blank);
}

/*                                  CONFIRM                                   */

/*
** Fixed-format victim line: two leading spaces, the PID left-justified in
** a TT_CONFIRM_PID_COL-wide field, one space, then the image name
** shortened to TT_CONFIRM_IMG_MAX cells. This is deliberately a FIXED
** format, not one measured from the actual n victims handed to
** draw_confirm(): a subtree kill can list an unbounded number of
** processes, so sizing the box from every one of their real widths would
** mean scanning (and potentially formatting) all n up front even when
** only a handful end up visible. A fixed worst-case width is exactly
** enough to size the box correctly - overlay_trunc() below still
** right-truncates anything that turns out longer than expected, the same
** safety net draw_help() already relies on for its own content lines.
*/
#define TT_CONFIRM_PID_COL     7
#define TT_CONFIRM_IMG_MAX     40
/*
** Wide enough to hold the longest title confirm_resolve_and_draw() can
** compose UNTRUNCATED: "kill process " (13) + a 10-digit PID + " (" (2)
** + up to TT_IMAGE_LEN - 1 (63) image characters + ")?" (2) = 90 wchar_t,
** plus the terminator. overlay_trunc()'s "fits within max" branch trusts
** `max` (inner_w, which tracks this same worst case via
** confirm_inner_width()) without separately checking it against the
** destination buffer's own capacity `n` - if this were smaller than that
** worst case, a long enough title on a wide enough terminal would get
** silently cut off by swprintf's own bound instead of properly
** ellipsis-truncated, the exact defect class the brief's width-invariant
** warning is about.
*/
#define TT_CONFIRM_LINE_BUF    100
#define TT_CONFIRM_PROMPT      L"Terminate? [y/N]"

static void confirm_victim_line(const t_process *p, wchar_t *out, size_t n)
{
    wchar_t img[TT_CONFIRM_IMG_MAX + 1];

    fmt_shorten(p->image, TT_CONFIRM_IMG_MAX, img,
            TT_CONFIRM_IMG_MAX + 1);
    swprintf(out, n, L"  %-*lu %ls", TT_CONFIRM_PID_COL, p->key.pid, img);
}

/*
** Worst-case natural width across every line draw_confirm() might emit:
** the title (caller-composed, arbitrary but short in practice), the
** prompt, a full-width victim line and the widest plausible "... and N
** more" line. See confirm_victim_line()'s own comment for why this is a
** fixed bound rather than a scan over the real victim list.
*/
static int  confirm_inner_width(const wchar_t *title)
{
    int         w;
    int         vw;
    wchar_t     more[TT_CONFIRM_LINE_BUF];

    w = (int)wcslen(title);
    if ((int)wcslen(TT_CONFIRM_PROMPT) > w)
        w = (int)wcslen(TT_CONFIRM_PROMPT);
    vw = 2 + TT_CONFIRM_PID_COL + 1 + TT_CONFIRM_IMG_MAX;
    if (vw > w)
        w = vw;
    swprintf(more, TT_CONFIRM_LINE_BUF, L"  ... and %d more", 9999999);
    if ((int)wcslen(more) > w)
        w = (int)wcslen(more);
    return (w);
}

static void confirm_content_row(t_frame *f, int left_pad, int right_pad,
                            int inner_w, const wchar_t *text)
{
    wchar_t buf[TT_CONFIRM_LINE_BUF];

    frame_pad(f, left_pad);
    frame_puts(f, L"\u2502 ");
    overlay_trunc(text, inner_w, buf, TT_CONFIRM_LINE_BUF);
    frame_printf(f, L"%-*ls", inner_w, buf);
    frame_puts(f, L" \u2502");
    frame_pad(f, right_pad);
    frame_puts(f, L"\r\n");
}

static void confirm_blank_row(t_frame *f, int left_pad, int right_pad,
                            int inner_w)
{
    frame_pad(f, left_pad);
    frame_puts(f, L"\u2502 ");
    frame_pad(f, inner_w);
    frame_puts(f, L" \u2502");
    frame_pad(f, right_pad);
    frame_puts(f, L"\r\n");
}

/*
** The Task 20 kill confirmation overlay - see render.h for the full
** contract. Structured exactly like draw_help() (box degrades on cols,
** then on rows, then refuses to draw at all below a minimum viable size)
** with one addition: content rows are always exactly [title, up to
** (content_cap - 2) victim/"+more" lines, ..., prompt] - title and prompt
** are the two reserved end slots that NEVER lose to victim lines, which
** is what guarantees "still show the count and the y/N prompt" (the
** brief's own ambiguity resolution #3) even when there is no room for a
** single victim.
**
** box_h is clamped against `n` (the natural content height, 2 + n) the
** same way draw_help() clamps against its own fixed binding count - n is
** bounded defensively here (TT_CONFIRM_MAX_NATURAL) purely so that
** arithmetic on it can never overflow int, not because a real subtree is
** ever remotely that large.
*/
#define TT_CONFIRM_MAX_NATURAL  100000

void    draw_confirm(t_frame *f, const wchar_t *title, t_process **victims,
                    size_t n, int cols, int rows)
{
    size_t      n_bounded;
    int         inner_desired;
    int         box_w;
    int         box_h;
    int         inner_w;
    int         content_cap;
    int         victim_slots;
    int         victims_shown;
    int         more_count;
    int         filler;
    int         left_pad;
    int         right_pad;
    int         top_blank;
    int         bottom_blank;
    wchar_t     line[TT_CONFIRM_LINE_BUF];
    int         i;

    n_bounded = n;
    if (n_bounded > TT_CONFIRM_MAX_NATURAL)
        n_bounded = TT_CONFIRM_MAX_NATURAL;
    inner_desired = confirm_inner_width(title);
    box_w = cols;
    if (box_w > inner_desired + 4)
        box_w = inner_desired + 4;
    box_h = rows;
    if (box_h < 0)
        box_h = 0;
    if (box_h > (int)n_bounded + 4)
        box_h = (int)n_bounded + 4;
    if (box_w < 5 || box_h < 4)
    {
        overlay_blank_rows(f, cols, rows);
        return ;
    }
    inner_w = box_w - 4;
    content_cap = box_h - 2;
    victim_slots = content_cap - 2;
    if ((size_t)victim_slots >= n)
    {
        victims_shown = (int)n;
        more_count = 0;
    }
    else if (victim_slots >= 1)
    {
        victims_shown = victim_slots - 1;
        more_count = (int)(n - (size_t)victims_shown);
    }
    else
    {
        victims_shown = 0;
        more_count = 0;
    }
    left_pad = (cols - box_w) / 2;
    right_pad = cols - box_w - left_pad;
    top_blank = (rows - box_h) / 2;
    bottom_blank = rows - box_h - top_blank;
    overlay_blank_rows(f, cols, top_blank);
    overlay_border_row(f, left_pad, right_pad, inner_w, 1);
    confirm_content_row(f, left_pad, right_pad, inner_w, title);
    i = 0;
    while (i < victims_shown)
    {
        confirm_victim_line(victims[i], line, TT_CONFIRM_LINE_BUF);
        confirm_content_row(f, left_pad, right_pad, inner_w, line);
        i++;
    }
    if (more_count > 0)
    {
        swprintf(line, TT_CONFIRM_LINE_BUF, L"  ... and %d more",
                more_count);
        confirm_content_row(f, left_pad, right_pad, inner_w, line);
    }
    filler = victim_slots - victims_shown - (more_count > 0 ? 1 : 0);
    while (filler > 0)
    {
        confirm_blank_row(f, left_pad, right_pad, inner_w);
        filler--;
    }
    confirm_content_row(f, left_pad, right_pad, inner_w, TT_CONFIRM_PROMPT);
    overlay_border_row(f, left_pad, right_pad, inner_w, 0);
    overlay_blank_rows(f, cols, bottom_blank);
}

/*
** Resolves a->confirm_victims (KEYS, captured when F9/Shift+F9 opened
** the dialog - see app.h) against the CURRENT a->cur, builds the caller
** title, and draws the overlay. This runs every frame the dialog stays
** open, not once at open time: a->cur can be swapped out from under it
** by an app_sample() that happens while the user is still deciding (the
** event loop does not pause sampling just because a dialog is up), and a
** victim that has exited in the meantime must disappear from what is
** shown rather than being drawn as "about to die" when it no longer
** exists. This is display-only - it changes nothing about which
** processes src/main.c actually calls plat_kill() on when 'y' lands, and
** it is NOT the safety-critical re-check itself: that one happens inside
** plat_kill() against a live handle, immediately before TerminateProcess,
** which is the only place close enough to the actual kill for the check
** to mean anything.
**
** shown[] is sized to a->confirm_count (the count captured at open time,
** an upper bound - resolution can only ever remove entries, never add
** them) rather than to a->cur.count, since the confirm dialog's own
** victim set is what is being displayed, not the whole live table.
*/
static void confirm_resolve_and_draw(t_frame *f, t_app *a, int cols,
                    int rows)
{
    t_process   **shown;
    size_t      n;
    size_t      i;
    t_process   *p;
    wchar_t     title[TT_CONFIRM_LINE_BUF];

    shown = NULL;
    n = 0;
    if (a->confirm_count > 0)
        shown = malloc(sizeof(t_process *) * a->confirm_count);
    if (shown != NULL)
    {
        i = 0;
        while (i < a->confirm_count)
        {
            p = table_find(&a->cur, a->confirm_victims[i]);
            if (p != NULL)
                shown[n++] = p;
            i++;
        }
    }
    if (a->confirm_subtree)
        swprintf(title, TT_CONFIRM_LINE_BUF,
                L"kill subtree - %llu process%ls?",
                (unsigned long long)n, n == 1 ? L"" : L"es");
    else if (n == 1)
        swprintf(title, TT_CONFIRM_LINE_BUF, L"kill process %lu (%ls)?",
                shown[0]->key.pid, shown[0]->image);
    else
        swprintf(title, TT_CONFIRM_LINE_BUF, L"kill process?");
    draw_confirm(f, title, shown, n, cols, rows);
    free(shown);
}

/*                                 RENDER_ALL                                 */

/*
** The only function main() calls once Task 19 wires the event loop in:
** clears the frame buffer, opens the terminal frame with home-and-erase
** (see the brief - deliberate, not dirty-region tracking), then composes
** header, meters, table and footer in that fixed order.
**
** `rows` is a hard ceiling, not a hint: header, meters and footer used to
** be drawn unconditionally before the table's own budget was clamped to
** zero, which meant a `rows` too small to fit all three still got all
** three - the exact defect class Task 16's gauge shipped with (a comment
** asserting a bound the code did not actually enforce). draw_help proves
** the right shape for this: box_h is clamped to `rows` BEFORE any content
** arithmetic runs, so nothing downstream can push the total past it. This
** function now does the equivalent - it decides which sections survive a
** short `rows` BEFORE drawing any of them, by spending a `budget` in
** priority order:
**
**   1. footer (1 line) - carries 'q', the one binding nothing else on
**      screen hints at (the same reasoning draw_footer's own key-bar
**      ordering already uses for its content). Kept unless `rows` is 0.
**   2. header (1 line) - identifies what is on screen and carries the
**      limited-mode marker. Second most protected.
**   3. meters (2 or 3 lines, cols-dependent) - load figures, real but the
**      most dispensable of the four sections; dropped whole rather than
**      partially (draw_meters has no notion of "draw fewer lines").
**   4. table - whatever budget is left, always, per the brief's own "the
**      table simply gets fewer rows". Can be (and often is) 0.
**
** Each section is included only if the FULL amount it needs still fits
** the remaining budget, so the running total of lines actually emitted
** can never exceed `rows`: every reservation below is subtracted from
** `budget` before the next one is attempted, and `table_rows` is
** whatever is left over, never negative.
**
** There is no persisted scroll offset anywhere in t_app - Task 17's
** draw_table takes `top` from its caller, and nothing between here and
** there owns remembering it frame to frame. This function therefore
** recomputes a scroll window fresh every call: it flattens the current
** table through the live view FIRST (both to know `nrows` - needed for
** the header's "N / M procs" - and to locate a->selected by key;
** a->selected is a (pid, create_time) key precisely so it survives
** table_add() reallocs and row reordering - see app.h), and centres the
** visible window on that row when it is found, then clamps so a
** near-the-end selection still fills the last page fully rather than
** leaving it half-empty. sel is left as (size_t)-1 - "no row selected",
** the same sentinel test_draw_table.c already uses - when a->selected
** matches nothing in the current frame (including the state a freshly
** zero-initialised t_app starts in, or a selected process that has since
** exited); Task 19 is responsible for pointing a->selected at a real row
** before the first frame the user should see something highlighted in.
**
** Task 20's kill confirmation is modal: while a->confirm_open, this
** function draws NOTHING else - no header, meters, table or footer, just
** confirm_resolve_and_draw()'s overlay filling the entire cols x rows
** frame - and returns immediately after. That mirrors how a live
** terminal dialog actually works (nothing behind it should be readable
** or misread as still-interactive) and, just as importantly, keeps the
** victim list's own screen space unconstrained by the header/meters/
** footer budget math below, which exists to serve a completely different
** section set.
*/
void    render_all(t_frame *f, t_app *a, int cols, int rows, int limited)
{
    t_process   **rowbuf;
    size_t      nrows;
    size_t      sel;
    size_t      i;
    int         show_footer;
    int         show_header;
    int         show_meters;
    int         meter_lines;
    int         budget;
    int         table_rows;
    int         top;

    frame_reset(f);
    frame_puts(f, L"\x1b[H\x1b[2J");
    if (a->confirm_open)
    {
        confirm_resolve_and_draw(f, a, cols, rows);
        return ;
    }
    rowbuf = NULL;
    nrows = 0;
    if (a->cur.count > 0)
    {
        rowbuf = malloc(sizeof(t_process *) * a->cur.count);
        if (rowbuf != NULL)
            nrows = view_flatten(&a->cur, &a->view, rowbuf, a->cur.count);
    }
    sel = (size_t)-1;
    i = 0;
    while (i < nrows)
    {
        if (key_eq(rowbuf[i]->key, a->selected))
        {
            sel = i;
            break ;
        }
        i++;
    }
    budget = rows;
    if (budget < 0)
        budget = 0;
    show_footer = (budget >= 1);
    if (show_footer)
        budget -= 1;
    show_header = (budget >= 1);
    if (show_header)
        budget -= 1;
    meter_lines = (cols >= 100) ? 3 : 2;
    show_meters = (budget >= meter_lines);
    if (show_meters)
        budget -= meter_lines;
    table_rows = budget;
    if (show_header)
        draw_header(f, a, cols, limited, nrows);
    if (show_meters)
        draw_meters(f, &a->sys, cols);
    top = 0;
    if (sel != (size_t)-1 && table_rows > 0)
    {
        top = (int)sel - table_rows / 2;
        if (top < 0)
            top = 0;
        if ((size_t)top + (size_t)table_rows > nrows)
        {
            top = (int)nrows - table_rows;
            if (top < 0)
                top = 0;
        }
    }
    draw_table(f, rowbuf, nrows, sel, top, cols, table_rows, &a->view);
    free(rowbuf);
    if (show_footer)
        draw_footer(f, a, cols);
}
