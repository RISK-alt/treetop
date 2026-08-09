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

void    test_tree(void)
{
    test_links();
    test_pid_reuse();
    test_missing_parent_is_root();
    test_self_parent_is_root();
    test_subtree_aggregates();
    test_collapsed_hides_descendants();
    test_cycle_does_not_hang();
}
