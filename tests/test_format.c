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

    /* Regression: with n == 1 the caller has room for exactly one
       wchar_t. The max < 2 branch used to write out[0] and out[1]
       unconditionally, one wchar_t past a buffer this small. Confirm the
       call now stays inside its declared capacity: out[0] becomes a
       valid empty string, and the guard value placed right after it is
       left untouched. */
    {
        wchar_t guard[2];

        guard[0] = L'?';
        guard[1] = L'#';
        fmt_shorten(L"abcdef", 1, guard, 1);
        TT_EQ_WSTR(guard, L"");
        TT_EQ_INT(guard[1], L'#');
    }
}

/*
** fmt_command is the COMMAND-column-specific counterpart to fmt_shorten:
** right-truncated (ellipsis trails) instead of left, because a command
** line's identifying token sits at the front, not the tail. Each case
** below is one of the five the review asked for by name.
*/
static void test_command(void)
{
    wchar_t buf[128];

    /* A quoted argv[0] with a long path: image supplies the short name,
       the quoting is consumed rather than needing hand-rolled parsing. */
    fmt_command(L"find.exe",
        L"\"C:\\Program Files\\Git\\usr\\bin\\find.exe\" / -iname winternl.h",
        100, buf, 128);
    TT_EQ_WSTR(buf, L"find / -iname winternl.h");

    /* An unquoted argv[0]. */
    fmt_command(L"node.exe", L"node server.js --port 3000", 100, buf, 128);
    TT_EQ_WSTR(buf, L"node server.js --port 3000");

    /* No argument tail at all: falls back to fmt_shorten on the bare
       stem, which fits here and so is returned unchanged - "node.exe"
       becomes "node", not the untouched original. */
    fmt_command(L"node.exe", L"node.exe", 100, buf, 128);
    TT_EQ_WSTR(buf, L"node");

    /* A single long path-like token with no tail, narrow enough that
       even the bare stem overflows: this is what actually reaches
       fmt_shorten's OWN internal truncation, not just its pass-through
       case - cross-checked against fmt_shorten directly so the exact
       cut point is never hand-counted here. */
    {
        wchar_t expected[64];

        fmt_shorten(L"verylongprocessnamehere", 10, expected, 64);
        fmt_command(L"verylongprocessnamehere.exe",
            L"\"C:\\some\\long\\nested\\path\\verylongprocessnamehere.exe\"",
            10, buf, 128);
        TT_EQ_WSTR(buf, expected);
    }

    /* Right-truncation landing mid-argument: "server.js" is cut to
       "serv", not on a word boundary, proving this is a hard character
       count and not some smarter word-aware truncation. */
    fmt_command(L"node.exe", L"node server.js --port 3000", 10, buf, 128);
    TT_EQ_WSTR(buf, L"node serv\u2026");
    TT_EQ_INT((int)wcslen(buf), 10);

    /* n bound respected even when max asks for more than the buffer can
       hold - the same class of bug fmt_shorten itself was once fixed
       for (commit cde8307). */
    {
        wchar_t guard[6];

        guard[0] = L'?'; guard[1] = L'?'; guard[2] = L'?';
        guard[3] = L'?'; guard[4] = L'?'; guard[5] = L'#';
        fmt_command(L"node.exe", L"node server.js --port 3000", 40, guard, 5);
        TT_EQ_INT((int)wcslen(guard) <= 4, 1);
        TT_EQ_INT(guard[5], L'#');
    }
}

void    test_format(void)
{
    test_bytes();
    test_duration();
    test_shorten();
    test_command();
}
