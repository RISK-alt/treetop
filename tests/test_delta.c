/* tests/test_delta.c */
#include "harness.h"
#include "process.h"

#include <string.h>

/* One second of wall clock in 100 ns units. */
#define SEC 10000000ULL

static void seed(t_table *t, unsigned long long when, unsigned int cores,
                 unsigned long long mem_total)
{
    TT_EQ_INT(table_init(t, 8), 0);
    t->sample_time = when;
    t->core_count = cores;
    t->mem_total = mem_total;
}

void    test_delta(void)
{
    t_table     prev;
    t_table     cur;
    t_process   p;

    seed(&prev, 1000 * SEC, 4, 8ULL * 1024 * 1024 * 1024);
    seed(&cur,  1001 * SEC, 4, 8ULL * 1024 * 1024 * 1024);

    /* First tick: no previous sample means 0 %, never a since-boot figure. */
    p = mk_proc(100, 500, 4, L"node.exe");
    p.user_time = 900 * SEC;
    p.working_set = 2ULL * 1024 * 1024 * 1024;
    table_add(&cur, &p);
    delta_apply(&cur, NULL);
    TT_EQ_DBL(cur.procs[0].cpu_pct, 0.0, 0.001);
    TT_EQ_DBL(cur.procs[0].mem_pct, 25.0, 0.01);

    /* One core fully busy for one second on a 4-core box is 25 %. */
    table_clear(&cur);
    p = mk_proc(100, 500, 4, L"node.exe");
    p.user_time = 10 * SEC;
    table_add(&prev, &p);
    p.user_time = 11 * SEC;
    table_add(&cur, &p);
    delta_apply(&cur, &prev);
    TT_EQ_DBL(cur.procs[0].cpu_pct, 25.0, 0.01);

    /* Kernel and user time both count. */
    table_clear(&prev);
    table_clear(&cur);
    p = mk_proc(101, 501, 4, L"cl.exe");
    p.user_time = 0; p.kernel_time = 0;
    table_add(&prev, &p);
    p.user_time = 2 * SEC; p.kernel_time = 2 * SEC;
    table_add(&cur, &p);
    delta_apply(&cur, &prev);
    TT_EQ_DBL(cur.procs[0].cpu_pct, 100.0, 0.01);

    /* Same PID, new creation time: a different process, so no match. */
    table_clear(&prev);
    table_clear(&cur);
    p = mk_proc(102, 500, 4, L"node.exe");
    p.user_time = 50 * SEC;
    table_add(&prev, &p);
    p = mk_proc(102, 900, 4, L"node.exe");
    p.user_time = 1 * SEC;
    table_add(&cur, &p);
    delta_apply(&cur, &prev);
    TT_EQ_DBL(cur.procs[0].cpu_pct, 0.0, 0.001);

    /* Zero-length interval must not divide by zero. */
    table_clear(&cur);
    cur.sample_time = prev.sample_time;
    p = mk_proc(103, 500, 4, L"a.exe");
    table_add(&prev, &p);
    p.user_time = 5 * SEC;
    table_add(&cur, &p);
    delta_apply(&cur, &prev);
    TT_EQ_DBL(cur.procs[0].cpu_pct, 0.0, 0.001);

    /* Cumulative time moving backwards is impossible: clamp, never negative. */
    table_clear(&prev);
    table_clear(&cur);
    cur.sample_time = prev.sample_time + SEC;
    p = mk_proc(104, 500, 4, L"a.exe");
    p.user_time = 100 * SEC;
    table_add(&prev, &p);
    p.user_time = 1 * SEC;
    table_add(&cur, &p);
    delta_apply(&cur, &prev);
    TT_EQ_DBL(cur.procs[0].cpu_pct, 0.0, 0.001);

    /* Timing jitter can make the busy delta exceed the wall interval.
       Clamp rather than print 180 %. */
    table_clear(&prev);
    table_clear(&cur);
    cur.sample_time = prev.sample_time + SEC;
    p = mk_proc(105, 500, 4, L"a.exe");
    p.user_time = 0;
    table_add(&prev, &p);
    p.user_time = 8 * SEC;      /* 8 core-seconds in 1 s on 4 cores */
    table_add(&cur, &p);
    delta_apply(&cur, &prev);
    TT_EQ_DBL(cur.procs[0].cpu_pct, 100.0, 0.001);

    table_free(&prev);
    table_free(&cur);
}
