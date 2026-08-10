/* tests/test_frame.c */
#include "harness.h"
#include "render.h"
#include "theme.h"

/*
** Strips CSI escape sequences (ESC '[' ... final-byte) to count only
** what would actually occupy a terminal cell. Originally this only
** recognised the 'm'-terminated SGR sequences (colour codes) Tasks 16
** and 17 emit - correct for them, but Task 18's render_all() opens every
** frame with "\x1b[H\x1b[2J" (cursor-home + erase-display), whose final
** bytes are 'H' and 'J', not 'm'. Under the old 'm'-only rule this
** parsed as "one giant unterminated escape" that swallowed everything up
** to the next literal 'm' it happened to find anywhere later in the
** buffer (a colour code, if any, or the end of the string) - silently
** undercounting the width of any line that carried a non-SGR escape.
** ECMA-48 defines a CSI sequence's final byte as anything in 0x40-0x7E,
** which covers 'H' and 'J' along with 'm' and everything else a real
** terminal parser recognises the same way; stopping at the first byte in
** that range is the general, correct rule, not a colour-specific one.
*/
size_t  visible_len(const wchar_t *s)
{
    size_t  n;

    n = 0;
    while (*s != L'\0')
    {
        if (*s == L'\x1b' && s[1] == L'[')
        {
            s += 2;
            while (*s != L'\0' && (*s < 0x40 || *s > 0x7e))
                s++;
            if (*s != L'\0')
                s++;
        }
        else if (*s == L'\r')
        {
            /* Carriage return moves the cursor, it does not occupy a
               column - draw_meters lines end "\r\n" and the '\n' is
               already the wcstok delimiter, so only '\r' needs this. */
            s++;
        }
        else
        {
            n++;
            s++;
        }
    }
    return (n);
}

/*
** Checks every line of a NUL-terminated frame (split on '\n', the "\r\n"
** line endings draw_meters writes) stays within cols once escapes are
** stripped. Empty trailing line from the final "\r\n" is skipped.
*/
static void check_lines_within(const wchar_t *text, int cols)
{
    wchar_t buf[8192];
    wchar_t *line;
    wchar_t *ctx;

    wcsncpy(buf, text, 8191);
    buf[8191] = L'\0';
    line = wcstok(buf, L"\n", &ctx);
    while (line != NULL)
    {
        TT_CHECK((int)visible_len(line) <= cols);
        line = wcstok(NULL, L"\n", &ctx);
    }
}

static void test_frame_buffer(void)
{
    t_frame f;

    TT_EQ_INT(frame_init(&f, 16), 0);

    frame_puts(&f, L"abc");
    TT_EQ_INT((int)f.len, 3);

    /* Growth past the initial capacity keeps everything. */
    frame_puts(&f, L"0123456789012345678901234567890");
    TT_EQ_INT((int)f.len, 34);
    TT_EQ_INT(f.buf[0] == L'a', 1);
    TT_EQ_INT(f.buf[33] == L'0', 1);

    frame_reset(&f);
    TT_EQ_INT((int)f.len, 0);

    frame_printf(&f, L"%d-%ls", 42, L"x");
    f.buf[f.len] = L'\0';
    TT_EQ_WSTR(f.buf, L"42-x");

    frame_reset(&f);
    frame_pad(&f, 4);
    TT_EQ_INT((int)f.len, 4);
    TT_EQ_INT(f.buf[0] == L' ', 1);

    /* With colour off, theme_load emits nothing at all. */
    g_color = 0;
    TT_EQ_WSTR(theme_load(99.0), L"");
    g_color = 1;
    TT_CHECK(wcslen(theme_load(99.0)) > 0);
    TT_CHECK(wcscmp(theme_load(5.0), theme_load(99.0)) != 0);

    frame_free(&f);
}

/*
** frame_pad must write EXACTLY n spaces, not just leave buf[0] as one.
** A bug that only sets the first cell and leaves the rest as whatever
** was in the (freshly malloc'd, uninitialised) buffer would pass the
** brief's own check but fail this one.
*/
static void test_pad_exact(void)
{
    t_frame f;
    int     i;

    TT_EQ_INT(frame_init(&f, 8), 0);
    frame_pad(&f, 5);
    TT_EQ_INT((int)f.len, 5);
    i = 0;
    while (i < 5)
    {
        TT_CHECK(f.buf[i] == L' ');
        i++;
    }
    /* Zero and negative widths write nothing. */
    frame_pad(&f, 0);
    TT_EQ_INT((int)f.len, 5);
    frame_pad(&f, -3);
    TT_EQ_INT((int)f.len, 5);
    frame_free(&f);
}

/*
** frame_printf must grow and retry rather than silently truncate when
** the formatted result is longer than any fixed scratch buffer an
** implementation might use internally. 300 characters is comfortably
** past any small stack buffer a naive vswprintf-based implementation
** would reach for.
*/
static void test_printf_long(void)
{
    t_frame f;
    wchar_t src[301];
    int     i;

    i = 0;
    while (i < 300)
    {
        src[i] = (wchar_t)(L'a' + (i % 26));
        i++;
    }
    src[300] = L'\0';
    TT_EQ_INT(frame_init(&f, 4), 0);
    frame_printf(&f, L"[%ls]", src);
    TT_EQ_INT((int)f.len, 302);
    f.buf[f.len] = L'\0';
    TT_CHECK(f.buf[0] == L'[');
    TT_CHECK(f.buf[301] == L']');
    TT_CHECK(wmemcmp(f.buf + 1, src, 300) == 0);
    frame_free(&f);
}

/*
** Pins every threshold boundary named in the brief: under 25% green,
** under 50% yellow-green, under 75% amber, 75% and above red. Sampling
** only two far-apart values (as the brief's own test does) cannot tell
** a correct threshold from one that is off by a few percentage points
** anywhere in the middle of the range - these pairs straddle each
** boundary by a tenth of a percent on either side.
*/
static void test_load_thresholds(void)
{
    g_color = 1;

    /* Same tier on both sides of 24.9/0 - both green. */
    TT_CHECK(wcscmp(theme_load(0.0), theme_load(24.9)) == 0);
    /* Crosses the 25% boundary: green -> yellow-green. */
    TT_CHECK(wcscmp(theme_load(24.9), theme_load(25.0)) != 0);

    /* Same tier either side of 49.9 - both yellow-green. */
    TT_CHECK(wcscmp(theme_load(25.0), theme_load(49.9)) == 0);
    /* Crosses the 50% boundary: yellow-green -> amber. */
    TT_CHECK(wcscmp(theme_load(49.9), theme_load(50.0)) != 0);

    /* Same tier either side of 74.9 - both amber. */
    TT_CHECK(wcscmp(theme_load(50.0), theme_load(74.9)) == 0);
    /* Crosses the 75% boundary: amber -> red. */
    TT_CHECK(wcscmp(theme_load(74.9), theme_load(75.0)) != 0);

    /* Top of the range stays red. */
    TT_CHECK(wcscmp(theme_load(75.0), theme_load(100.0)) == 0);
}

static t_sysinfo   mk_sys(unsigned int cores, double cpu_pct)
{
    t_sysinfo   s;
    unsigned int i;

    s.core_count = cores;
    s.cpu_pct = cpu_pct;
    i = 0;
    while (i < cores)
    {
        s.core_pct[i] = 100.0;
        i++;
    }
    s.mem_total = 34359738368ULL;
    s.mem_used = 17179869184ULL;
    s.uptime_secs = 3600;
    return (s);
}

/*
** Below 100 columns the per-core strip must not appear at all; at or
** above 100 it must. A "Cores" marker only draw_core_strip emits lets
** the test tell the two cases apart without depending on exact glyphs.
*/
static void test_core_strip_width(void)
{
    t_frame     f;
    t_sysinfo   sys;

    sys = mk_sys(8, 42.0);

    TT_EQ_INT(frame_init(&f, 256), 0);
    draw_meters(&f, &sys, 99);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"Cores") == NULL);
    frame_free(&f);

    TT_EQ_INT(frame_init(&f, 256), 0);
    draw_meters(&f, &sys, 100);
    f.buf[f.len] = L'\0';
    TT_CHECK(wcsstr(f.buf, L"Cores") != NULL);
    frame_free(&f);
}

/*
** A full-load, full-core-count frame is the worst case for overflowing a
** narrow terminal: every gauge bar is entirely filled and the core strip
** (when present) draws one glyph per core. No rendered line may exceed
** its declared width once colour escapes - which occupy zero terminal
** cells - are stripped out.
**
** 120/100/80 alone only exercise draw_gauge's full "<label>[<bar>]
** <value>" layout, where the bar simply absorbs whatever room is left.
** The interesting failure region is narrower than that: 40 and 20 still
** fit the full layout for these value lengths, but 10, 5, 1 and 0 force
** draw_gauge's value-then-label-then-nothing degradation (see the
** comment on draw_gauge) - a regression there would overflow exactly at
** these widths, not at 80+.
*/
static void test_meters_bounded(void)
{
    t_frame     f;
    t_sysinfo   sys;
    int         cols[9];
    int         i;

    sys = mk_sys(128, 100.0);
    cols[0] = 120;
    cols[1] = 100;
    cols[2] = 80;
    cols[3] = 40;
    cols[4] = 20;
    cols[5] = 10;
    cols[6] = 5;
    cols[7] = 1;
    cols[8] = 0;
    i = 0;
    while (i < 9)
    {
        TT_EQ_INT(frame_init(&f, 256), 0);
        draw_meters(&f, &sys, cols[i]);
        f.buf[f.len] = L'\0';
        check_lines_within(f.buf, cols[i]);
        frame_free(&f);
        i++;
    }
}

void    test_frame(void)
{
    test_frame_buffer();
    test_pad_exact();
    test_printf_long();
    test_load_thresholds();
    test_core_strip_width();
    test_meters_bounded();
}
