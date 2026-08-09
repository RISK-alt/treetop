/* tests/test_format.c */
#include "harness.h"
#include "render.h"

static void test_bytes(void)
{
    wchar_t buf[32];

    fmt_bytes(0, buf, 32);              TT_EQ_WSTR(buf, L"0");
    fmt_bytes(512, buf, 32);            TT_EQ_WSTR(buf, L"512");
    fmt_bytes(1024, buf, 32);           TT_EQ_WSTR(buf, L"1.0K");
    fmt_bytes(1019904, buf, 32);        TT_EQ_WSTR(buf, L"996K");
    fmt_bytes(188743680ULL, buf, 32);   TT_EQ_WSTR(buf, L"180M");
    fmt_bytes(1503238553ULL, buf, 32);  TT_EQ_WSTR(buf, L"1.4G");
    /* At or above 10 the decimal is dropped, so this is "32G" not "32.0G". */
    fmt_bytes(34359738368ULL, buf, 32); TT_EQ_WSTR(buf, L"32G");
}

static void test_duration(void)
{
    wchar_t buf[32];

    fmt_duration(0, buf, 32);       TT_EQ_WSTR(buf, L"0:00");
    fmt_duration(41, buf, 32);      TT_EQ_WSTR(buf, L"0:41");
    fmt_duration(723, buf, 32);     TT_EQ_WSTR(buf, L"12:03");
    fmt_duration(11051, buf, 32);   TT_EQ_WSTR(buf, L"3:04:11");
    fmt_duration(273851, buf, 32);  TT_EQ_WSTR(buf, L"3d 04:04");
}

static void test_shorten(void)
{
    wchar_t buf[64];

    /* Fits: unchanged. */
    fmt_shorten(L"node server.js", 32, buf, 64);
    TT_EQ_WSTR(buf, L"node server.js");

    /* Too long: keep the last (max - 1) characters, prefix with an
       ellipsis, for exactly max characters total. The source is 38 chars,
       so max 18 keeps the final 17. */
    fmt_shorten(L"C:\\Users\\x\\proj\\node_modules\\.bin\\vite", 18, buf, 64);
    TT_EQ_WSTR(buf, L"\u2026modules\\.bin\\vite");
    TT_EQ_INT((int)wcslen(buf), 18);

    /* Degenerate width still produces a valid string. */
    fmt_shorten(L"abcdef", 2, buf, 64);
    TT_EQ_INT((int)wcslen(buf) <= 2, 1);
}

void    test_format(void)
{
    test_bytes();
    test_duration();
    test_shorten();
}
