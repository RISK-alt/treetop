/* tests/test_view.c */
#include "harness.h"
#include "input.h"
#include "agent.h"

#include <string.h>
#include <wchar.h>

static int  contains_pid(t_process **rows, size_t n, unsigned long pid)
{
    size_t  i;

    for (i = 0; i < n; i++)
        if (rows[i]->key.pid == pid)
            return (1);
    return (0);
}

static void test_filter_match(void)
{
    t_process   p;

    p = mk_proc(1, 1, 0, L"node.exe");
    p.cmdline = L"node vite dev --host";

    TT_EQ_INT(filter_match(&p, L""), 1);        /* empty matches all */
    TT_EQ_INT(filter_match(&p, L"node"), 1);    /* image */
    TT_EQ_INT(filter_match(&p, L"NODE"), 1);    /* case-insensitive */
    TT_EQ_INT(filter_match(&p, L"vite"), 1);    /* cmdline */
    TT_EQ_INT(filter_match(&p, L"python"), 0);

    p.cmdline = NULL;
    TT_EQ_INT(filter_match(&p, L"vite"), 0);    /* NULL cmdline is safe */
}

static void test_filter_keeps_ancestors(void)
{
    t_table     t;
    t_view      v;
    t_process   p;
    t_process   *rows[16];
    size_t      n;

    table_init(&t, 8);
    p = mk_proc(100, 1000, 4, L"claude.exe");  table_add(&t, &p);
    p = mk_proc(200, 2000, 100, L"node.exe");
    p.cmdline = L"node vite dev";              table_add(&t, &p);
    p = mk_proc(300, 3000, 100, L"pwsh.exe");  table_add(&t, &p);
    tree_build(&t);

    view_init(&v);
    wcscpy(v.filter, L"vite");
    n = view_flatten(&t, &v, rows, 16);

    /* The match plus its parent for context; the sibling is gone. */
    TT_EQ_INT((int)n, 2);
    TT_EQ_INT((int)rows[0]->key.pid, 100);
    TT_EQ_INT((int)rows[1]->key.pid, 200);
    table_free(&t);
}

/*
** A whole branch where nothing matches - not merely one sibling of a
** matching row - must vanish entirely. test_filter_keeps_ancestors only
** proves a non-matching sibling is dropped; this proves subtree_matches's
** recursive negative case, three levels deep, with a second root that
** does match to make sure filtering is not accidentally "match anything
** anywhere in the table".
*/
static void test_filter_excludes_nonmatching_subtree(void)
{
    t_table     t;
    t_view      v;
    t_process   p;
    t_process   *rows[16];
    size_t      n;

    table_init(&t, 8);
    p = mk_proc(100, 1000, 4, L"alpha.exe");     table_add(&t, &p);
    p = mk_proc(200, 2000, 100, L"beta.exe");
    p.cmdline = L"beta run";                     table_add(&t, &p);
    p = mk_proc(300, 3000, 200, L"gamma.exe");
    p.cmdline = L"gamma work";                   table_add(&t, &p);
    p = mk_proc(400, 4000, 4, L"delta.exe");
    p.cmdline = L"python delta";                 table_add(&t, &p);
    tree_build(&t);

    view_init(&v);
    wcscpy(v.filter, L"python");
    n = view_flatten(&t, &v, rows, 16);

    TT_EQ_INT((int)n, 1);
    TT_EQ_INT((int)rows[0]->key.pid, 400);
    table_free(&t);
}

static void test_agents_only(void)
{
    t_table     t;
    t_view      v;
    t_process   p;
    t_process   *rows[16];
    size_t      n;

    table_init(&t, 8);
    p = mk_proc(100, 1000, 4, L"claude.exe");   table_add(&t, &p);
    p = mk_proc(200, 2000, 100, L"node.exe");   table_add(&t, &p);
    p = mk_proc(300, 3000, 4, L"explorer.exe"); table_add(&t, &p);
    p = mk_proc(400, 4000, 999, L"node.exe");   table_add(&t, &p); /* orphan */
    tree_build(&t);
    agent_classify(&t);

    view_init(&v);
    v.agents_only = 1;
    n = view_flatten(&t, &v, rows, 16);

    /* Session root, its child, and the orphan. explorer is gone. */
    TT_EQ_INT((int)n, 3);
    /* Explicit membership, not just a count: an implementation keying on
       is_agent_root instead of in_session would drop the session CHILD
       (200) while still passing a bare count of 3 by accident only if it
       also miscounted elsewhere, so check every expected member and the
       excluded one by name. */
    TT_CHECK(contains_pid(rows, n, 100));
    TT_CHECK(contains_pid(rows, n, 200));
    TT_CHECK(contains_pid(rows, n, 400));
    TT_CHECK(!contains_pid(rows, n, 300));
    table_free(&t);
}

/*
** The negative half of agents_only on its own: a process that is neither
** in a session nor an orphan must be excluded even when it is the ONLY
** row in the table. A test that only ever checks a mixed table could pass
** by coincidence of counts; this cannot.
*/
static void test_agents_only_excludes_bystander(void)
{
    t_table     t;
    t_view      v;
    t_process   p;
    t_process   *rows[16];
    size_t      n;

    table_init(&t, 8);
    p = mk_proc(500, 5000, 4, L"explorer.exe"); table_add(&t, &p);
    tree_build(&t);
    agent_classify(&t);

    view_init(&v);
    v.agents_only = 1;
    n = view_flatten(&t, &v, rows, 16);

    TT_EQ_INT((int)n, 0);
    table_free(&t);
}

static void test_orphans_only(void)
{
    t_table     t;
    t_view      v;
    t_process   p;
    t_process   *rows[16];
    size_t      n;

    table_init(&t, 8);
    p = mk_proc(100, 1000, 4, L"claude.exe");    table_add(&t, &p);
    p = mk_proc(200, 2000, 999, L"node.exe");    table_add(&t, &p); /* orphan */
    p = mk_proc(300, 3000, 4, L"explorer.exe");  table_add(&t, &p);
    tree_build(&t);

    view_init(&v);
    v.orphans_only = 1;
    n = view_flatten(&t, &v, rows, 16);

    TT_EQ_INT((int)n, 1);
    TT_EQ_INT((int)rows[0]->key.pid, 200);
    table_free(&t);
}

static void test_sort_flat(void)
{
    t_table     t;
    t_view      v;
    t_process   p;
    t_process   *rows[16];
    size_t      n;

    table_init(&t, 8);
    p = mk_proc(100, 1000, 4, L"a.exe"); p.cpu_pct = 5.0;  table_add(&t, &p);
    p = mk_proc(200, 2000, 4, L"b.exe"); p.cpu_pct = 50.0; table_add(&t, &p);
    p = mk_proc(300, 3000, 4, L"c.exe"); p.cpu_pct = 20.0; table_add(&t, &p);
    tree_build(&t);

    view_init(&v);
    v.tree_mode = 0;
    v.sort = SORT_CPU;
    v.sort_desc = 1;
    n = view_flatten(&t, &v, rows, 16);
    TT_EQ_INT((int)n, 3);
    TT_EQ_INT((int)rows[0]->key.pid, 200);
    TT_EQ_INT((int)rows[1]->key.pid, 300);
    TT_EQ_INT((int)rows[2]->key.pid, 100);

    v.sort = SORT_PID;
    v.sort_desc = 0;
    view_flatten(&t, &v, rows, 16);
    TT_EQ_INT((int)rows[0]->key.pid, 100);
    table_free(&t);
}

/*
** sort_desc must genuinely invert the comparator, not merely be ignored.
** Checked at every index, both directions, off the same data.
*/
static void test_sort_desc_reverses(void)
{
    t_table     t;
    t_view      v;
    t_process   p;
    t_process   *rows[16];

    table_init(&t, 8);
    p = mk_proc(100, 1000, 4, L"a.exe"); table_add(&t, &p);
    p = mk_proc(200, 2000, 4, L"b.exe"); table_add(&t, &p);
    p = mk_proc(300, 3000, 4, L"c.exe"); table_add(&t, &p);
    tree_build(&t);

    view_init(&v);
    v.tree_mode = 0;
    v.sort = SORT_PID;
    v.sort_desc = 0;
    view_flatten(&t, &v, rows, 16);
    TT_EQ_INT((int)rows[0]->key.pid, 100);
    TT_EQ_INT((int)rows[1]->key.pid, 200);
    TT_EQ_INT((int)rows[2]->key.pid, 300);

    v.sort_desc = 1;
    view_flatten(&t, &v, rows, 16);
    TT_EQ_INT((int)rows[0]->key.pid, 300);
    TT_EQ_INT((int)rows[1]->key.pid, 200);
    TT_EQ_INT((int)rows[2]->key.pid, 100);
    table_free(&t);
}

/*
** Equal sort keys must keep the order the tree gave them. Rows are added
** to the table with distinct pids but IDENTICAL cpu_pct, so any sort that
** is not stable (e.g. compares with >= instead of > and swaps equal rows)
** will shuffle this order; ascending and descending are both checked since
** an unstable sort could accidentally look stable in only one direction.
*/
static void test_sort_stability(void)
{
    t_table     t;
    t_view      v;
    t_process   p;
    t_process   *rows[16];

    table_init(&t, 8);
    p = mk_proc(100, 1000, 4, L"a.exe"); p.cpu_pct = 10.0; table_add(&t, &p);
    p = mk_proc(200, 2000, 4, L"b.exe"); p.cpu_pct = 10.0; table_add(&t, &p);
    p = mk_proc(300, 3000, 4, L"c.exe"); p.cpu_pct = 10.0; table_add(&t, &p);
    p = mk_proc(400, 4000, 4, L"d.exe"); p.cpu_pct = 10.0; table_add(&t, &p);
    tree_build(&t);

    view_init(&v);
    v.tree_mode = 0;
    v.sort = SORT_CPU;

    v.sort_desc = 0;
    view_flatten(&t, &v, rows, 16);
    TT_EQ_INT((int)rows[0]->key.pid, 100);
    TT_EQ_INT((int)rows[1]->key.pid, 200);
    TT_EQ_INT((int)rows[2]->key.pid, 300);
    TT_EQ_INT((int)rows[3]->key.pid, 400);

    v.sort_desc = 1;
    view_flatten(&t, &v, rows, 16);
    TT_EQ_INT((int)rows[0]->key.pid, 100);
    TT_EQ_INT((int)rows[1]->key.pid, 200);
    TT_EQ_INT((int)rows[2]->key.pid, 300);
    TT_EQ_INT((int)rows[3]->key.pid, 400);
    table_free(&t);
}

/* In tree mode the sort column orders siblings; the hierarchy survives. */
static void test_sort_siblings_in_tree(void)
{
    t_table     t;
    t_view      v;
    t_process   p;
    t_process   *rows[16];
    size_t      n;

    table_init(&t, 8);
    p = mk_proc(100, 1000, 4, L"root.exe"); p.cpu_pct = 1.0;  table_add(&t, &p);
    p = mk_proc(200, 2000, 100, L"a.exe");  p.cpu_pct = 5.0;  table_add(&t, &p);
    p = mk_proc(300, 3000, 100, L"b.exe");  p.cpu_pct = 60.0; table_add(&t, &p);
    p = mk_proc(400, 4000, 100, L"c.exe");  p.cpu_pct = 20.0; table_add(&t, &p);
    tree_build(&t);

    view_init(&v);              /* tree_mode = 1, SORT_CPU, descending */
    n = view_flatten(&t, &v, rows, 16);

    TT_EQ_INT((int)n, 4);
    TT_EQ_INT((int)rows[0]->key.pid, 100);   /* the root stays first */
    TT_EQ_INT((int)rows[1]->key.pid, 300);   /* 60 % */
    TT_EQ_INT((int)rows[2]->key.pid, 400);   /* 20 % */
    TT_EQ_INT((int)rows[3]->key.pid, 200);   /* 5 %  */
    TT_EQ_INT(rows[1]->depth, 1);            /* still children, not flat */
    table_free(&t);
}

/*
** Roots are sorted too, same comparator: the busiest session floats to
** the top even though it never entered a sibling list.
*/
static void test_sort_roots_in_tree(void)
{
    t_table     t;
    t_view      v;
    t_process   p;
    t_process   *rows[16];
    size_t      n;

    table_init(&t, 8);
    p = mk_proc(100, 1000, 4, L"quiet.exe");
    p.cpu_pct = 1.0;    table_add(&t, &p);
    p = mk_proc(200, 2000, 4, L"busy.exe");
    p.cpu_pct = 90.0;   table_add(&t, &p);
    p = mk_proc(300, 3000, 4, L"medium.exe");
    p.cpu_pct = 40.0;   table_add(&t, &p);
    tree_build(&t);

    view_init(&v);
    n = view_flatten(&t, &v, rows, 16);
    TT_EQ_INT((int)n, 3);
    TT_EQ_INT((int)rows[0]->key.pid, 200);
    TT_EQ_INT((int)rows[1]->key.pid, 300);
    TT_EQ_INT((int)rows[2]->key.pid, 100);
    table_free(&t);
}

static void test_view_init_defaults(void)
{
    t_view  v;

    memset(&v, 0xAA, sizeof(v));
    view_init(&v);
    TT_EQ_INT((int)v.sort, (int)SORT_CPU);
    TT_EQ_INT(v.sort_desc, 1);
    TT_EQ_INT(v.tree_mode, 1);
    TT_EQ_INT(v.agents_only, 0);
    TT_EQ_INT(v.orphans_only, 0);
    TT_EQ_INT(v.filter[0], L'\0');
}

/*
** roots[VIEW_MAX_ROOTS] is a fixed 1024-entry scratch array by design.
** Feed it well past that so a real overrun (or a lost/duplicated node
** from a bad bound) would show up rather than being coincidentally
** avoided by a small table. Every process here has ppid 0, which
** tree_build resolves to a root with no lookup needed, so this stays
** fast even at this size.
*/
static void test_roots_truncation(void)
{
    t_table     t;
    t_view      v;
    t_process   p;
    t_process   *rows[2000];
    size_t      n;
    unsigned long i;

    table_init(&t, 8);
    for (i = 0; i < 1030; i++)
    {
        p = mk_proc(i + 1, 1000 + i, 0, L"proc.exe");
        table_add(&t, &p);
    }
    tree_build(&t);

    view_init(&v);
    n = view_flatten(&t, &v, rows, 2000);

    /* Truncates at the cap rather than overrunning or failing outright. */
    TT_EQ_INT((int)n, 1024);
    TT_EQ_INT((int)rows[0]->key.pid, 1);
    TT_EQ_INT((int)rows[1023]->key.pid, 1024);
    table_free(&t);
}

void    test_view(void)
{
    test_filter_match();
    test_filter_keeps_ancestors();
    test_filter_excludes_nonmatching_subtree();
    test_agents_only();
    test_agents_only_excludes_bystander();
    test_orphans_only();
    test_sort_flat();
    test_sort_desc_reverses();
    test_sort_stability();
    test_sort_siblings_in_tree();
    test_sort_roots_in_tree();
    test_view_init_defaults();
    test_roots_truncation();
}
