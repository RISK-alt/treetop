#include "render.h"
#include "theme.h"

/*
** This file is portable C - no Win32 - so it lives in treetop_core and
** treetop_tests can exercise it against a synthetic t_sysinfo.
**
** Colour discipline: the only colour source anywhere in this file is
** theme_load(). A gauge's fill is wrapped in whatever theme_load(pct)
** returns and closed with TT_RESET only when that call actually returned
** something (color[0] != L'\0') - with g_color off, theme_load() already
** returns L"", so this file never has to read g_color itself to stay
** silent, and TT_RESET is never emitted without a matching opener.
**
** Block-drawing glyphs below are written as \u escapes rather than
** literal characters, per house style - a literal glyph in source is
** encoding-fragile across editors and toolchains.
*/

/*                                  CPU GAUGE                                 */

/*
** Renders one gauge line, never more than `cols` visible columns wide -
** though below a certain width it draws less than the full
** "<label>[<bar>] <value>" layout. Three fixed degradation steps, each
** gated on the exact byte budget the dropped layout needs:
**
**   1. Full layout, if "<label>[" + bar + "] <value>" fits (>= 7+vlen).
**   2. Drop the value first - the bar already shows roughly the same
**      information, so the value is the most redundant part - if
**      "<label>[" + bar + "]" still fits (>= 6).
**   3. Drop the label too - CPU is always drawn before MEM, so screen
**      position still identifies which gauge is which - if "[" + bar +
**      "]" fits (>= 2).
**   4. Below that, draw nothing at all rather than emit a truncated
**      bracket or a bar clipped mid-glyph.
**
** Each step's threshold is the exact width that step needs with an
** empty (zero-width) bar, so whichever step is chosen always fits
** within cols with room left over for the bar - no combination of
** label, value length and terminal width can push the composed line
** past cols.
*/
static void draw_gauge(t_frame *f, const wchar_t *label, double pct,
                        const wchar_t *value, int cols)
{
    int             vlen;
    int             bar_w;
    int             filled;
    int             i;
    int             show_label;
    int             show_value;
    double          clamped;
    const wchar_t   *color;

    if (cols < 2)
        return ;
    clamped = pct;
    if (clamped < 0.0)
        clamped = 0.0;
    if (clamped > 100.0)
        clamped = 100.0;
    vlen = (int)wcslen(value);
    show_value = (cols >= 7 + vlen);
    show_label = (cols >= 6);
    if (show_value)
        bar_w = cols - 7 - vlen;
    else if (show_label)
        bar_w = cols - 6;
    else
        bar_w = cols - 2;
    if (show_label)
        frame_printf(f, L"%-4ls[", label);
    else
        frame_puts(f, L"[");
    filled = (int)(clamped / 100.0 * (double)bar_w + 0.5);
    if (filled > bar_w)
        filled = bar_w;
    color = theme_load(clamped);
    frame_puts(f, color);
    i = 0;
    while (i < filled)
    {
        frame_puts(f, L"\u2588");
        i++;
    }
    while (i < bar_w)
    {
        frame_puts(f, L"\u2591");
        i++;
    }
    if (color[0] != L'\0')
        frame_puts(f, TT_RESET);
    frame_puts(f, L"]");
    if (show_value)
        frame_printf(f, L" %ls", value);
}

/*                                 CORE STRIP                                 */

/*
** One block-height glyph per core, each individually load-coloured.
** Bounded to `cols` by capping the core count drawn at however many
** glyphs fit after the "Cores: " label, never at the real core_count.
*/
static void draw_core_strip(t_frame *f, const t_sysinfo *sys, int cols)
{
    static const wchar_t   *glyph[8] = {
        L"\u2581", L"\u2582", L"\u2583", L"\u2584",
        L"\u2585", L"\u2586", L"\u2587", L"\u2588",
    };
    int             avail;
    unsigned int    n;
    unsigned int    i;
    int             level;
    double          pct;
    const wchar_t   *color;

    frame_puts(f, L"Cores: ");
    avail = cols - 7;
    if (avail < 0)
        avail = 0;
    n = sys->core_count;
    if (n > (unsigned int)avail)
        n = (unsigned int)avail;
    i = 0;
    while (i < n)
    {
        pct = sys->core_pct[i];
        if (pct < 0.0)
            pct = 0.0;
        if (pct > 100.0)
            pct = 100.0;
        level = (int)(pct / 100.0 * 7.0 + 0.5);
        if (level > 7)
            level = 7;
        color = theme_load(pct);
        frame_puts(f, color);
        frame_puts(f, glyph[level]);
        if (color[0] != L'\0')
            frame_puts(f, TT_RESET);
        i++;
    }
    frame_puts(f, L"\r\n");
}

/*                                   METERS                                   */

/*
** CPU and memory gauges, plus - only at 100 columns or wider - a
** per-core strip. Narrower than that, the strip is dropped entirely
** rather than wrapped or clipped mid-glyph.
*/
void    draw_meters(t_frame *f, const t_sysinfo *sys, int cols)
{
    wchar_t cpu_val[16];
    wchar_t mem_used[16];
    wchar_t mem_total[16];
    wchar_t mem_val[40];
    double  mem_pct;

    swprintf(cpu_val, 16, L"%5.1f%%", sys->cpu_pct);
    draw_gauge(f, L"CPU", sys->cpu_pct, cpu_val, cols);
    frame_puts(f, L"\r\n");
    fmt_bytes(sys->mem_used, mem_used, 16);
    fmt_bytes(sys->mem_total, mem_total, 16);
    swprintf(mem_val, 40, L"%ls/%ls", mem_used, mem_total);
    mem_pct = 0.0;
    if (sys->mem_total > 0)
        mem_pct = (double)sys->mem_used / (double)sys->mem_total * 100.0;
    draw_gauge(f, L"MEM", mem_pct, mem_val, cols);
    frame_puts(f, L"\r\n");
    if (cols >= 100)
        draw_core_strip(f, sys, cols);
}
