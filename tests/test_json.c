/* tests/test_json.c */
#include "harness.h"
#include "render.h"

#include <math.h>
#include <string.h>

/*
** Reads back everything written to a tmpfile() so far, NUL-terminated -
** same convention tests/test_cli.c already uses for cli_print_help()/
** cli_print_version(), reused here so json_emit() can be checked against
** its real output without hijacking the process's own stdout.
*/
static void read_back(FILE *fp, char *buf, size_t n)
{
    size_t  got;

    rewind(fp);
    got = fread(buf, 1, n - 1, fp);
    buf[got] = '\0';
}

/*
** json_emit() (src/render/json.c) is the one public entry point of the
** --json flag - README.md calls this shape "a stable interface from the
** first tagged release on" - and had no test of its own anywhere: only
** its two building blocks, json_escape() and json_utf8_encode(), were
** covered above. Exercises a synthetic table built by hand (no tree_build
** needed - json_emit reads is_agent_root/is_orphan/subtree_* directly off
** each row, it does not walk parent/child links) covering every section
** and the edge cases most likely to silently corrupt the contract: a NULL
** cmdline, a backslash-heavy one, and a non-finite cpu_pct.
*/
static void test_json_emit_shape_and_edge_cases(void)
{
    t_table     tbl;
    t_sysinfo   sys;
    t_process   p;
    FILE        *fp;
    char        buf[8192];
    char        *v_version;
    char        *v_system;
    char        *v_sessions;
    char        *v_orphans;
    char        *v_processes;

    TT_EQ_INT(table_init(&tbl, 8), 0);
    memset(&sys, 0, sizeof(sys));
    sys.core_count = 8;
    sys.cpu_pct = 45.5;
    sys.mem_total = 34000000000ULL;
    sys.mem_used = 12000000000ULL;

    /* An agent session root: drives the "sessions" array. */
    p = mk_proc(100, 1000, 4, L"claude.exe");
    p.is_agent_root = 1;
    p.agent_label = L"Claude Code";
    p.subtree_cpu = 12.5;
    p.subtree_mem = 1000000;
    p.subtree_count = 3;
    TT_CHECK(table_add(&tbl, &p) != NULL);

    /* An orphan with a listening port and a non-finite cpu_pct - the
       "finite_or_zero" guard must turn NAN into 0.0, never emit "nan"
       (which is not valid JSON and would break every consumer's parser). */
    p = mk_proc(200, 2000, 999, L"node.exe");
    p.is_orphan = 1;
    p.cmdline = L"node server.js";
    p.port_count = 1;
    p.ports[0] = 3000;
    p.working_set = 500000;
    p.cpu_pct = NAN;
    TT_CHECK(table_add(&tbl, &p) != NULL);

    /* A NULL cmdline (protected process) must serialise as JSON null,
       never the literal text "(null)" and never a crash. */
    p = mk_proc(300, 3000, 4, L"System.exe");
    p.cmdline = NULL;
    TT_CHECK(table_add(&tbl, &p) != NULL);

    /* A backslash-heavy Windows command line must escape every backslash
       - the single most common character JSON needs escaped in any real
       command line this tool ever shows. */
    p = mk_proc(400, 4000, 4, L"a.exe");
    p.cmdline = L"C:\\Users\\test\\a.exe --flag \"C:\\Program Files\\x\"";
    TT_CHECK(table_add(&tbl, &p) != NULL);

    fp = tmpfile();
    TT_CHECK(fp != NULL);
    if (fp == NULL)
    {
        table_free(&tbl);
        return ;
    }
    json_emit(&tbl, &sys, fp);
    read_back(fp, buf, sizeof(buf));
    fclose(fp);
    table_free(&tbl);

    /* Every top-level key is present, and in the documented order. */
    v_version = strstr(buf, "\"version\":");
    v_system = strstr(buf, "\"system\":");
    v_sessions = strstr(buf, "\"sessions\":");
    v_orphans = strstr(buf, "\"orphans\":");
    v_processes = strstr(buf, "\"processes\":");
    TT_CHECK(v_version != NULL);
    TT_CHECK(v_system != NULL);
    TT_CHECK(v_sessions != NULL);
    TT_CHECK(v_orphans != NULL);
    TT_CHECK(v_processes != NULL);
    if (v_version && v_system && v_sessions && v_orphans && v_processes)
    {
        TT_CHECK(v_version < v_system);
        TT_CHECK(v_system < v_sessions);
        TT_CHECK(v_sessions < v_orphans);
        TT_CHECK(v_orphans < v_processes);
    }
    TT_CHECK(strstr(buf, "\"cores\": 8") != NULL);

    /* The session entry sits inside "sessions", ahead of "orphans". */
    if (v_sessions != NULL && v_orphans != NULL)
    {
        char    *session_pid;

        session_pid = strstr(v_sessions, "\"pid\": 100");
        TT_CHECK(session_pid != NULL && session_pid < v_orphans);
        TT_CHECK(strstr(v_sessions, "Claude Code") != NULL);
    }

    /* The orphan entry: ports array, and NAN guarded to a plain 0.0. */
    if (v_orphans != NULL && v_processes != NULL)
    {
        char    *orphan_pid;

        orphan_pid = strstr(v_orphans, "\"pid\": 200");
        TT_CHECK(orphan_pid != NULL && orphan_pid < v_processes);
        TT_CHECK(strstr(v_orphans, "\"ports\": [3000]") != NULL);
        TT_CHECK(strstr(v_orphans, "\"cpu_pct\": 0.0") != NULL);
        TT_CHECK(strstr(v_orphans, "nan") == NULL);
        TT_CHECK(strstr(v_orphans, "NAN") == NULL);
    }

    /* NULL cmdline -> JSON null, never the four characters "(null)". */
    TT_CHECK(strstr(buf, "\"pid\": 300, \"ppid\": 4, \"image\": \"System.exe\", "
            "\"cmdline\": null") != NULL);
    TT_CHECK(strstr(buf, "(null)") == NULL);

    /* Backslash-heavy cmdline escapes every backslash. */
    TT_CHECK(strstr(buf,
            "C:\\\\Users\\\\test\\\\a.exe --flag \\\"C:\\\\Program Files"
            "\\\\x\\\"") != NULL);
}

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

    /*
    ** Truncation must never leave a trailing half-escape.
    **
    ** The input mixes widths on purpose. An all-backslash input cannot
    ** discriminate: every escape is exactly 2 wide, so the output length
    ** is always even and truncation lands on a boundary whatever the
    ** implementation does - a naive per-character loop and an off-by-one
    ** bound both pass it. Here "ab" fills 2 of the 3 usable slots, so the
    ** backslash's 2-wide escape cannot fit and must be dropped whole.
    ** A naive implementation emits "ab\" and fails.
    */
    json_escape(L"ab\\cd", buf, 4);
    TT_EQ_WSTR(buf, L"ab");

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

    test_json_emit_shape_and_edge_cases();
}
