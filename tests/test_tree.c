/* tests/test_tree.c */
#include "harness.h"
#include "process.h"

#include <string.h>

static void test_links(void)
{
    t_table     t;
    t_process   p;
    t_process   *rows[16];
    size_t      n;

    table_init(&t, 8);
    p = mk_proc(100, 1000, 4, L"claude.exe");  table_add(&t, &p);
    p = mk_proc(200, 2000, 100, L"node.exe");  table_add(&t, &p);
    p = mk_proc(300, 3000, 100, L"pwsh.exe");  table_add(&t, &p);
    p = mk_proc(400, 4000, 200, L"rg.exe");    table_add(&t, &p);
    tree_build(&t);

    TT_CHECK(t.procs[0].parent == NULL);
    TT_CHECK(t.procs[1].parent == &t.procs[0]);
    TT_CHECK(t.procs[3].parent == &t.procs[1]);
    TT_EQ_INT(t.procs[0].depth, 0);
    TT_EQ_INT(t.procs[1].depth, 1);
    TT_EQ_INT(t.procs[3].depth, 2);

    /* Siblings keep table order. */
    TT_CHECK(t.procs[0].first_child == &t.procs[1]);
    TT_CHECK(t.procs[1].next_sibling == &t.procs[2]);

    n = tree_flatten(&t, rows, 16);
    TT_EQ_INT((int)n, 4);
    TT_EQ_INT((int)rows[0]->key.pid, 100);
    TT_EQ_INT((int)rows[1]->key.pid, 200);
    TT_EQ_INT((int)rows[2]->key.pid, 400);   /* depth-first: rg before pwsh */
    TT_EQ_INT((int)rows[3]->key.pid, 300);
    table_free(&t);
}

static void test_pid_reuse(void)
{
    t_table     t;
    t_process   p;

    table_init(&t, 8);
    /* The child is OLDER than the process now holding its parent PID,
       so that PID was recycled and the real parent is gone. */
    p = mk_proc(100, 5000, 4, L"newcomer.exe"); table_add(&t, &p);
    p = mk_proc(200, 1000, 100, L"node.exe");   table_add(&t, &p);
    tree_build(&t);

    TT_CHECK(t.procs[1].parent == NULL);
    TT_EQ_INT(t.procs[1].depth, 0);
    table_free(&t);
}

static void test_missing_parent_is_root(void)
{
    t_table     t;
    t_process   p;

    table_init(&t, 8);
    p = mk_proc(200, 1000, 999, L"node.exe"); table_add(&t, &p);
    tree_build(&t);
    TT_CHECK(t.procs[0].parent == NULL);
    TT_EQ_INT(t.procs[0].depth, 0);
    table_free(&t);
}

static void test_self_parent_is_root(void)
{
    t_table     t;
    t_process   p;

    table_init(&t, 8);
    p = mk_proc(4, 0, 4, L"System"); table_add(&t, &p);
    tree_build(&t);
    TT_CHECK(t.procs[0].parent == NULL);
    table_free(&t);
}

static void test_subtree_aggregates(void)
{
    t_table     t;
    t_process   p;

    table_init(&t, 8);
    p = mk_proc(100, 1000, 4, L"claude.exe");
    p.cpu_pct = 2.0;  p.working_set = 100; table_add(&t, &p);
    p = mk_proc(200, 2000, 100, L"node.exe");
    p.cpu_pct = 30.0; p.working_set = 200; table_add(&t, &p);
    p = mk_proc(300, 3000, 200, L"rg.exe");
    p.cpu_pct = 5.0;  p.working_set = 50;  table_add(&t, &p);
    tree_build(&t);

    TT_EQ_DBL(t.procs[0].subtree_cpu, 37.0, 0.01);
    TT_EQ_INT((int)t.procs[0].subtree_mem, 350);
    TT_EQ_INT(t.procs[0].subtree_count, 3);      /* includes itself */
    TT_EQ_INT(t.procs[2].subtree_count, 1);
    table_free(&t);
}

static void test_collapsed_hides_descendants(void)
{
    t_table     t;
    t_process   p;
    t_process   *rows[16];
    size_t      n;

    table_init(&t, 8);
    p = mk_proc(100, 1000, 4, L"claude.exe"); table_add(&t, &p);
    p = mk_proc(200, 2000, 100, L"node.exe"); table_add(&t, &p);
    p = mk_proc(300, 3000, 200, L"rg.exe");   table_add(&t, &p);
    tree_build(&t);
    t.procs[0].collapsed = 1;

    n = tree_flatten(&t, rows, 16);
    TT_EQ_INT((int)n, 1);
    TT_EQ_INT((int)rows[0]->key.pid, 100);
    table_free(&t);
}

static void test_cycle_does_not_hang(void)
{
    t_table     t;
    t_process   p;
    t_process   *rows[16];

    table_init(&t, 8);
    /* Mutually-parented pair. Impossible in reality, cheap to guard. */
    p = mk_proc(100, 1000, 200, L"a.exe"); table_add(&t, &p);
    p = mk_proc(200, 2000, 100, L"b.exe"); table_add(&t, &p);
    tree_build(&t);
    TT_CHECK(tree_flatten(&t, rows, 16) <= 2);
    table_free(&t);
}

static void test_flatten_respects_max(void)
{
    t_table     t;
    t_process   p;
    t_process   *rows[8];
    size_t      n;
    int         i;

    table_init(&t, 8);
    p = mk_proc(100, 1000, 4, L"root.exe"); table_add(&t, &p);
    p = mk_proc(200, 2000, 100, L"a.exe");  table_add(&t, &p);
    p = mk_proc(300, 3000, 200, L"b.exe");  table_add(&t, &p);
    p = mk_proc(400, 4000, 300, L"c.exe");  table_add(&t, &p);
    tree_build(&t);

    /* Four nodes into a buffer of two: exactly two written, and the
       slot past the limit must be left alone. */
    for (i = 0; i < 8; i++)
        rows[i] = NULL;
    n = tree_flatten(&t, rows, 2);
    TT_EQ_INT((int)n, 2);
    TT_CHECK(rows[2] == NULL);

    /* max == 0 writes nothing at all. */
    rows[0] = NULL;
    n = tree_flatten(&t, rows, 0);
    TT_EQ_INT((int)n, 0);
    TT_CHECK(rows[0] == NULL);

    table_free(&t);
}

static void test_ppid_zero_is_root(void)
{
    t_table     t;
    t_process   p;

    table_init(&t, 8);
    /* pid 0 must actually be present in the table, otherwise the lookup
       fails on its own and the short-circuit is not what is under test.
       It is older than the child, so without the ppid == 0 check the
       child would be parented to System Idle Process. */
    p = mk_proc(0, 0, 0, L"System Idle Process"); table_add(&t, &p);
    p = mk_proc(500, 5000, 0, L"svchost.exe");    table_add(&t, &p);
    tree_build(&t);

    TT_CHECK(t.procs[1].parent == NULL);
    TT_EQ_INT(t.procs[1].depth, 0);
    table_free(&t);
}

static void test_orphans(void)
{
    t_table     t;
    t_process   p;

    table_init(&t, 8);

    /* Dev runtime with a dead parent: orphan. */
    p = mk_proc(200, 1000, 999, L"node.exe");   table_add(&t, &p);

    /* Non-dev process with a dead parent: NOT an orphan. Windows is full
       of these and flagging them would drown the signal. */
    p = mk_proc(201, 1000, 999, L"explorer.exe"); table_add(&t, &p);

    /* Non-dev, dead parent, but holding a port: orphan. */
    p = mk_proc(202, 1000, 999, L"mystery.exe");
    p.ports[0] = 3000; p.port_count = 1;        table_add(&t, &p);

    /* Dev runtime with a live parent: not an orphan. */
    p = mk_proc(100, 500, 4, L"claude.exe");    table_add(&t, &p);
    p = mk_proc(203, 1000, 100, L"node.exe");   table_add(&t, &p);

    /* ppid 0 is a true system root, never an orphan. */
    p = mk_proc(4, 0, 0, L"System");            table_add(&t, &p);

    /* ppid 0 AND a dev-runtime image: still never an orphan. Without the
       ppid == 0 guard this would be wrongly flagged, and unlike the
       System row above it would not survive the guard's deletion by
       accident - it is specifically built to trip the other qualifiers. */
    p = mk_proc(6, 0, 0, L"node.exe");          table_add(&t, &p);

    tree_build(&t);

    TT_EQ_INT(t.procs[0].is_orphan, 1);
    TT_EQ_INT(t.procs[1].is_orphan, 0);
    TT_EQ_INT(t.procs[2].is_orphan, 1);
    TT_EQ_INT(t.procs[4].is_orphan, 0);
    TT_EQ_INT(t.procs[5].is_orphan, 0);
    TT_EQ_INT(t.procs[6].is_orphan, 0);
    table_free(&t);
}

static void test_dev_runtime_names(void)
{
    TT_EQ_INT(is_dev_runtime(L"node.exe"), 1);
    TT_EQ_INT(is_dev_runtime(L"NODE.EXE"), 1);     /* case-insensitive */
    TT_EQ_INT(is_dev_runtime(L"node"), 1);         /* extension optional */
    TT_EQ_INT(is_dev_runtime(L"pwsh.exe"), 1);
    TT_EQ_INT(is_dev_runtime(L"dotnet.exe"), 1);
    TT_EQ_INT(is_dev_runtime(L"explorer.exe"), 0);
    TT_EQ_INT(is_dev_runtime(L"nodepad.exe"), 0);  /* prefix must not match */
    TT_EQ_INT(is_dev_runtime(L""), 0);
}

void    test_tree(void)
{
    test_links();
    test_pid_reuse();
    test_missing_parent_is_root();
    test_self_parent_is_root();
    test_subtree_aggregates();
    test_collapsed_hides_descendants();
    test_cycle_does_not_hang();
    test_flatten_respects_max();
    test_ppid_zero_is_root();
    test_orphans();
    test_dev_runtime_names();
}
