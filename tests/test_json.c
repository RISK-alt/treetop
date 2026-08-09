/* tests/test_json.c */
#include "harness.h"
#include "render.h"

#include <string.h>

void    test_json(void)
{
    wchar_t buf[256];
    char    utf8[64];
    size_t  n;

    json_escape(L"node server.js", buf, 256);
    TT_EQ_WSTR(buf, L"node server.js");

    /* Backslashes are everywhere in Windows paths. */
    json_escape(L"C:\\Users\\x\\a.js", buf, 256);
    TT_EQ_WSTR(buf, L"C:\\\\Users\\\\x\\\\a.js");

    json_escape(L"say \"hi\"", buf, 256);
    TT_EQ_WSTR(buf, L"say \\\"hi\\\"");

    json_escape(L"a\tb\nc", buf, 256);
    TT_EQ_WSTR(buf, L"a\\tb\\nc");

    /* Control characters become \u escapes. */
    json_escape(L"a\x01" L"b", buf, 256);
    TT_EQ_WSTR(buf, L"a\\u0001b");

    /* NULL input yields an empty string, not a crash. */
    json_escape(NULL, buf, 256);
    TT_EQ_WSTR(buf, L"");

    /* Truncation must never leave a trailing half-escape. */
    json_escape(L"\\\\\\\\\\\\", buf, 5);
    TT_CHECK(wcslen(buf) < 5);

    /* UTF-8 encoder: plain ASCII passes through as one byte each. */
    n = json_utf8_encode(L"Hi", utf8, sizeof(utf8));
    TT_EQ_INT(n, 2);
    TT_CHECK(memcmp(utf8, "Hi", 3) == 0);

    /* 2-byte range: e-acute, U+00E9. */
    {
        wchar_t w2[2];

        w2[0] = 0x00E9;
        w2[1] = L'\0';
        n = json_utf8_encode(w2, utf8, sizeof(utf8));
        TT_EQ_INT(n, 2);
        TT_CHECK((unsigned char)utf8[0] == 0xC3
                && (unsigned char)utf8[1] == 0xA9);
    }

    /* 3-byte range: rightwards arrow, U+2192. */
    {
        wchar_t w3[2];

        w3[0] = 0x2192;
        w3[1] = L'\0';
        n = json_utf8_encode(w3, utf8, sizeof(utf8));
        TT_EQ_INT(n, 3);
        TT_CHECK((unsigned char)utf8[0] == 0xE2
                && (unsigned char)utf8[1] == 0x86
                && (unsigned char)utf8[2] == 0x92);
    }

    /* Surrogate pair: grinning face emoji, U+1F600. */
    {
        wchar_t w4[3];

        w4[0] = (wchar_t)0xD83D;
        w4[1] = (wchar_t)0xDE00;
        w4[2] = L'\0';
        n = json_utf8_encode(w4, utf8, sizeof(utf8));
        TT_EQ_INT(n, 4);
        TT_CHECK((unsigned char)utf8[0] == 0xF0
                && (unsigned char)utf8[1] == 0x9F
                && (unsigned char)utf8[2] == 0x98
                && (unsigned char)utf8[3] == 0x80);
    }

    /*
    ** An unpaired high surrogate followed by an ASCII character must not
    ** be encoded literally (that would be invalid UTF-8) and must not
    ** consume the character after it - it becomes U+FFFD alone, and the
    ** following 'x' is still there afterwards.
    */
    {
        wchar_t w5[3];

        w5[0] = (wchar_t)0xD800;
        w5[1] = L'x';
        w5[2] = L'\0';
        n = json_utf8_encode(w5, utf8, sizeof(utf8));
        TT_EQ_INT(n, 4);
        TT_CHECK((unsigned char)utf8[0] == 0xEF
                && (unsigned char)utf8[1] == 0xBF
                && (unsigned char)utf8[2] == 0xBD
                && utf8[3] == 'x');
    }

    /*
    ** An unpaired high surrogate as the very last unit before the NUL
    ** must not read past the terminator while checking for its partner.
    */
    {
        wchar_t w6[2];

        w6[0] = (wchar_t)0xD800;
        w6[1] = L'\0';
        n = json_utf8_encode(w6, utf8, sizeof(utf8));
        TT_EQ_INT(n, 3);
        TT_CHECK((unsigned char)utf8[0] == 0xEF
                && (unsigned char)utf8[1] == 0xBF
                && (unsigned char)utf8[2] == 0xBD);
    }

    /* Truncation must never leave a partial multi-byte sequence. */
    {
        wchar_t w7[3];
        char    small[3];

        w7[0] = (wchar_t)0xD83D;
        w7[1] = (wchar_t)0xDE00;
        w7[2] = L'\0';
        n = json_utf8_encode(w7, small, sizeof(small));
        TT_EQ_INT(n, 0);
        TT_CHECK(small[0] == '\0');
    }
}
