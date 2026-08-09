/* tests/test_agent.c */
#include "harness.h"
#include "agent.h"

#include <string.h>

static void test_match(void)
{
    t_process   p;

    p = mk_proc(1, 1, 0, L"claude.exe");
    TT_CHECK(agent_match(&p) != NULL);

    /* node.exe with the agent name in argv: the case image matching misses. */
    p = mk_proc(2, 1, 0, L"node.exe");
    p.cmdline = L"node C:\\Users\\x\\AppData\\npm\\claude\\cli.js";
    TT_CHECK(agent_match(&p) != NULL);

    /* cursor is MATCH_IMAGE only: argv must not trigger it. */
    p = mk_proc(3, 1, 0, L"node.exe");
    p.cmdline = L"node build.js --target cursor";
    TT_CHECK(agent_match(&p) == NULL);

    /* Plain node is not an agent. */
    p = mk_proc(4, 1, 0, L"node.exe");
    p.cmdline = L"node server.js";
    TT_CHECK(agent_match(&p) == NULL);

    /* NULL cmdline must not crash. */
    p = mk_proc(5, 1, 0, L"node.exe");
    p.cmdline = NULL;
    TT_CHECK(agent_match(&p) == NULL);

    /* Case-insensitive. */
    p = mk_proc(6, 1, 0, L"Cursor.exe");
    TT_CHECK(agent_match(&p) != NULL);

    /* gemini is MATCH_CMDLINE only: an image merely named gemini.exe must
       not trigger it, and the NULL cmdline here must not crash either. */
    p = mk_proc(7, 1, 0, L"gemini.exe");
    TT_CHECK(agent_match(&p) == NULL);
}

/*
** Rule matching must be token-delimited, not bare substring: a rule like
** "amp" must not fire on a process whose command line merely happens to
** contain those letters embedded in a longer word (steampid=, mongoose).
** Confirmed live: steamwebhelper.exe's cmdline contains "-steampid=" and
** was reported as an Amp agent session before this fix.
*/
static void test_token_boundary(void)
{
    t_process   p;

    /* The exact regression: "amp" inside "-steampid=" must not match. */
    p = mk_proc(10, 1, 0, L"steamwebhelper.exe");
    p.cmdline = L"\"C:\\Program Files (x86)\\Steam\\steamwebhelper.exe\" "
                L"-nocrashdialog \"-steampid=1234\"";
    TT_CHECK(agent_match(&p) == NULL);

    /* "goose" embedded inside "mongoose" must not match. */
    p = mk_proc(11, 1, 0, L"mongoose.exe");
    p.cmdline = L"mongoose.exe --config db.conf";
    TT_CHECK(agent_match(&p) == NULL);

    /* Plain image match still works: needle at start, followed by '.'. */
    p = mk_proc(12, 1, 0, L"claude.exe");
    TT_CHECK(agent_match(&p) != NULL);

    /* Full quoted path: needle preceded by '\\', followed by '.', inside
       quotes at both ends of the haystack. */
    p = mk_proc(13, 1, 0, L"node.exe");
    p.cmdline = L"\"C:\\Users\\x\\.local\\bin\\claude.exe\"";
    TT_CHECK(agent_match(&p) != NULL);

    /* Path-component case MATCH_CMDLINE exists for: needle preceded and
       followed by a path separator, not at either end of the haystack. */
    p = mk_proc(14, 1, 0, L"node.exe");
    p.cmdline = L"node C:\\Users\\x\\AppData\\npm\\claude\\cli.js";
    TT_CHECK(agent_match(&p) != NULL);

    /* Match at the very start of the haystack. */
    p = mk_proc(15, 1, 0, L"node.exe");
    p.cmdline = L"claude --version";
    TT_CHECK(agent_match(&p) != NULL);

    /* Match at the very end of the haystack. */
    p = mk_proc(16, 1, 0, L"node.exe");
    p.cmdline = L"run-as-agent claude";
    TT_CHECK(agent_match(&p) != NULL);
}

static void test_classify(void)
{
    t_table     t;
    t_process   p;

    table_init(&t, 8);
    p = mk_proc(100, 1000, 4, L"claude.exe");   table_add(&t, &p);
    p = mk_proc(200, 2000, 100, L"node.exe");   table_add(&t, &p);
    /* A nested claude inside a session must NOT open a second session. */
    p = mk_proc(300, 3000, 200, L"claude.exe"); table_add(&t, &p);
    p = mk_proc(400, 4000, 4, L"cursor.exe");   table_add(&t, &p);
    /*
    ** A process in no session at all. Without it every in_session
    ** assertion in this test expects 1, and an implementation that
    ** simply hardcoded in_session = 1 would pass the whole suite -
    ** silently turning Task 8's agents-only view into a no-op.
    */
    p = mk_proc(500, 5000, 4, L"explorer.exe"); table_add(&t, &p);
    tree_build(&t);
    agent_classify(&t);

    TT_EQ_INT(t.procs[0].is_agent_root, 1);
    TT_EQ_WSTR(t.procs[0].agent_label, L"Claude Code");
    TT_EQ_INT(t.procs[1].is_agent_root, 0);
    TT_EQ_INT(t.procs[2].is_agent_root, 0);
    TT_EQ_INT(t.procs[3].is_agent_root, 1);
    TT_EQ_WSTR(t.procs[3].agent_label, L"Cursor");

    /* Everything under a session root is in the session, root included. */
    TT_EQ_INT(t.procs[0].in_session, 1);
    TT_EQ_INT(t.procs[1].in_session, 1);
    TT_EQ_INT(t.procs[2].in_session, 1);
    TT_EQ_INT(t.procs[3].in_session, 1);

    /* And the negative case, which is what makes the four above mean
       something: explorer belongs to no session. */
    TT_EQ_INT(t.procs[4].is_agent_root, 0);
    TT_EQ_INT(t.procs[4].in_session, 0);
    TT_CHECK(t.procs[4].agent_label == NULL);
    table_free(&t);
}

void    test_agent(void)
{
    test_match();
    test_token_boundary();
    test_classify();
}
