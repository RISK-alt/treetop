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
    table_free(&t);
}

void    test_agent(void)
{
    test_match();
    test_classify();
}
