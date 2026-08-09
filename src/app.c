#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
** app_sample() is the pipeline every later task builds on. The order
** below is load-bearing and must not be reshuffled:
**
**   1. swap cur into prev
**   2. table_clear(cur); cur.sample_time = plat_now()
**   3. plat_system(&sys); copy core_count/mem_total into cur
**   4. plat_processes(cur) - every table_add() finishes here
**   5. plat_cmdline() per process
**   6. plat_ports(cur), on its own 3 s wall-clock timer
**   7. delta_apply(cur, prev or NULL on the first tick)
**   8. tree_build(cur) - also marks orphans
**   9. agent_classify(cur)
**  10. carry collapsed state forward by key
**
** Step 4 matters more than the others: table_add() can realloc() the
** table's backing array, which invalidates every t_process * handed out
** before it returns. Nothing in this file takes such a pointer before
** plat_processes() is done - step 5 re-derives each process's address
** from cur.procs[i] by index, never from a pointer captured earlier.
*/

/*
** 3000 ms expressed in plat_now()'s units. plat_now() returns a Windows
** FILETIME value (100-nanosecond ticks), not milliseconds, so the port
** timer's threshold has to be converted once here rather than compared
** against a raw millisecond count.
*/
#define APP_PORT_INTERVAL_TICKS  (3000ULL * 10000ULL)

int     app_init(t_app *a)
{
    memset(a, 0, sizeof(*a));
    a->limited = plat_init();
    if (table_init(&a->cur, 256) != 0)
        return (-1);
    if (table_init(&a->prev, 256) != 0)
    {
        table_free(&a->cur);
        return (-1);
    }
    view_init(&a->view);
    a->running = 1;
    a->refresh_ms = 1000;
    a->first_tick = 1;
    return (0);
}

void    app_sample(t_app *a)
{
    t_table     tmp;
    size_t      i;
    t_process   *old;

    /*
    ** plat_cmdline_sweep() belongs right here, before anything else in
    ** this function touches cur or prev, and it sweeps against cur as it
    ** still stands from the end of the *previous* call - i.e. the
    ** freshest complete sample this process has ever taken, whose
    ** cmdline pointers are all still live.
    **
    ** The only table whose pointers this can invalidate is prev (two
    ** samples old at this instant): any of its rows whose key is no
    ** longer present in cur will be left with a dangling cmdline field.
    ** That is safe here specifically because prev is unconditionally
    ** about to be overwritten - table_clear() plus a fresh
    ** plat_processes() call, a few lines below - before this function
    ** returns control to any caller. Nothing, now or in any later task,
    ** gets a chance to read those dangling pointers.
    **
    ** Sweeping later instead - after step 10, against the table that has
    ** just become the new cur - would leave the dangling pointers sitting
    ** in prev for a full tick, readable by anything that touches prev in
    ** the meantime. That becomes a real use-after-free once a renderer
    ** exists, so it is deliberately not where this call lives.
    */
    plat_cmdline_sweep(&a->cur);

    /*
    ** 1. Swap: an ownership transfer of the two heap arrays, not a copy.
    ** Each t_table owns exactly one procs allocation at a time, and this
    ** struct assignment just exchanges which of the two local structs
    ** holds which pointer - nothing is allocated or freed here, so there
    ** is nothing to double-free and nothing leaks.
    */
    tmp = a->prev;
    a->prev = a->cur;
    a->cur = tmp;

    /* 2 */
    table_clear(&a->cur);
    a->cur.sample_time = plat_now();

    /* 3 */
    plat_system(&a->sys);
    a->cur.core_count = a->sys.core_count;
    a->cur.mem_total = a->sys.mem_total;

    /* 4. All table_add() calls happen inside plat_processes(). */
    plat_processes(&a->cur);

    /*
    ** 5. Indexed by position into cur.procs, never through a pointer
    ** taken before plat_processes() returned.
    */
    for (i = 0; i < a->cur.count; i++)
        a->cur.procs[i].cmdline = plat_cmdline(a->cur.procs[i].key);

    /*
    ** 6. Wall-clock port timer, deliberately independent of refresh_ms:
    ** tying it to tick count would mean a 15-second port lag at
    ** --refresh 5000. last_port_ms == 0 only on the very first sample
    ** (plat_now() never returns 0 for a real clock), so the first tick
    ** always refreshes ports immediately.
    */
    if (a->last_port_ms == 0
        || a->cur.sample_time - a->last_port_ms >= APP_PORT_INTERVAL_TICKS)
    {
        plat_ports(&a->cur);
        a->last_port_ms = a->cur.sample_time;
    }

    /* 7 */
    delta_apply(&a->cur, a->first_tick ? NULL : &a->prev);
    a->first_tick = 0;

    /* 8. Also marks orphans, as its final step. */
    tree_build(&a->cur);

    /* 9 */
    agent_classify(&a->cur);

    /*
    ** 10. Carry collapsed state forward by key. Every row in cur is
    ** freshly built by plat_processes() with collapsed reset to 0 (it is
    ** zeroed along with the rest of the struct wherever the table grows,
    ** and overwritten wholesale by table_add() otherwise), so without
    ** this loop the tree would re-collapse to fully expanded on every
    ** refresh. A process that vanished between ticks simply is not in
    ** cur, so the loop never touches it; a process whose PID was recycled
    ** gets a new create_time, so key_eq() correctly treats it as a
    ** different identity and its row starts uncollapsed rather than
    ** inheriting a stranger's state.
    **
    ** selected is a key, not an index or a t_process field, so it already
    ** carries forward unchanged with no copy needed - it keeps naming the
    ** same process across the swap regardless of where that process now
    ** sits in cur. A caller that needs to know whether the selection is
    ** still alive checks table_find(&a->cur, a->selected) itself; nothing
    ** here can do that check on the caller's behalf.
    */
    for (i = 0; i < a->cur.count; i++)
    {
        old = table_find(&a->prev, a->cur.procs[i].key);
        if (old != NULL)
            a->cur.procs[i].collapsed = old->collapsed;
    }
}

void    app_free(t_app *a)
{
    table_free(&a->cur);
    table_free(&a->prev);
    plat_shutdown();
}

/*                                SELFTEST                                    */

#define APP_ST_LABEL_WIDTH  30
#define APP_ST_MAX_LABELS   16

static void st_line(const char *label, int ok, const char *detail)
{
    int len;
    int dots;

    len = (int)strlen(label);
    dots = APP_ST_LABEL_WIDTH - len;
    if (dots < 3)
        dots = 3;
    printf("  %s ", label);
    while (dots-- > 0)
        printf(".");
    printf(" %s", ok ? "ok" : "FAIL");
    if (detail != NULL && detail[0] != '\0')
        printf("      %s", detail);
    printf("\n");
}

/*
** Distinct agent_label pointers among session roots. Every label is one
** of the static string literals in g_agent_rules, so two processes
** matched by the same rule share the exact same pointer - comparing
** pointers is enough to dedupe, no string comparison needed.
*/
static size_t   st_collect_labels(const t_table *tbl, const wchar_t **out,
                    size_t max)
{
    size_t  i;
    size_t  j;
    size_t  n;
    int     seen;

    n = 0;
    for (i = 0; i < tbl->count; i++)
    {
        if (!tbl->procs[i].is_agent_root)
            continue;
        seen = 0;
        for (j = 0; j < n; j++)
            if (out[j] == tbl->procs[i].agent_label)
                seen = 1;
        if (!seen && n < max)
            out[n++] = tbl->procs[i].agent_label;
    }
    return (n);
}

/*
** Joins up to n wide labels into a narrow, comma-separated buffer. Every
** label is ASCII (they are all literal English strings in rules.c), so
** wcstombs() under the default "C" locale is exact - no locale switch
** needed for text that never contains anything outside that range.
*/
static void st_join_labels(const wchar_t * const *labels, size_t n,
                char *out, size_t outsz)
{
    char    piece[64];
    size_t  used;
    size_t  i;
    int     written;

    out[0] = '\0';
    used = 0;
    for (i = 0; i < n && used < outsz; i++)
    {
        wcstombs(piece, labels[i], sizeof(piece) - 1);
        piece[sizeof(piece) - 1] = '\0';
        written = snprintf(out + used, outsz - used, "%s%s",
                (i == 0) ? "" : ", ", piece);
        if (written < 0 || (size_t)written >= outsz - used)
            break ;
        used += (size_t)written;
    }
}

/*
** ntdll resolution failing is reported honestly on its own line, but it
** does not by itself fail the whole selftest: plat_processes() falls
** back to ToolHelp32 and still produces a real process table, so the
** tool stays usable in that state (see ambiguity resolution #4). Process
** enumeration, system metrics and port enumeration are the collectors
** the rest of the app cannot function without, so only those three gate
** the return code. Unreadable command lines are a ratio, never a
** failure: a protected process refusing PROCESS_QUERY_LIMITED_INFORMATION
** is a Windows security boundary, not a bug. Zero agent sessions is a
** legitimate, expected result on a machine with nothing agentic running,
** so that line can never fail either.
*/
int     app_selftest(void)
{
    t_table         cur;
    t_sysinfo       sys;
    int             limited;
    int             ports_rc;
    size_t          i;
    size_t          readable;
    size_t          port_count;
    size_t          port_procs;
    size_t          root_count;
    int             max_depth;
    size_t          session_count;
    size_t          orphan_count;
    const wchar_t   *labels[APP_ST_MAX_LABELS];
    size_t          nlabels;
    char            detail[256];
    char            label_ascii[64];
    int             failed;

    printf("treetop selftest\n");
    limited = plat_init();
    st_line("ntdll entry points", limited == 0,
            limited == 0 ? "" : "falling back to ToolHelp32");
    if (table_init(&cur, 256) != 0)
    {
        printf("  out of memory - cannot continue\n");
        return (1);
    }
    cur.sample_time = plat_now();
    plat_processes(&cur);
    snprintf(detail, sizeof(detail), "%zu processes", cur.count);
    st_line("process enumeration", cur.count > 0, detail);
    readable = 0;
    for (i = 0; i < cur.count; i++)
    {
        cur.procs[i].cmdline = plat_cmdline(cur.procs[i].key);
        if (cur.procs[i].cmdline != NULL)
            readable++;
    }
    snprintf(detail, sizeof(detail), "%zu/%zu readable", readable,
            cur.count);
    st_line("command lines", cur.count > 0, detail);
    ports_rc = plat_ports(&cur);
    port_count = 0;
    port_procs = 0;
    for (i = 0; i < cur.count; i++)
    {
        if (cur.procs[i].port_count > 0)
            port_procs++;
        port_count += (size_t)cur.procs[i].port_count;
    }
    snprintf(detail, sizeof(detail), "%zu ports on %zu processes",
            port_count, port_procs);
    st_line("listening ports", ports_rc == 0, detail);
    plat_system(&sys);
    cur.core_count = sys.core_count;
    cur.mem_total = sys.mem_total;
    snprintf(detail, sizeof(detail), "%u cores, %.1f GB", sys.core_count,
            (double)sys.mem_total / (1024.0 * 1024.0 * 1024.0));
    st_line("system metrics", sys.core_count > 0 && sys.mem_total > 0,
            detail);
    tree_build(&cur);
    agent_classify(&cur);
    root_count = 0;
    max_depth = 0;
    orphan_count = 0;
    for (i = 0; i < cur.count; i++)
    {
        if (cur.procs[i].parent == NULL)
            root_count++;
        if (cur.procs[i].depth > max_depth)
            max_depth = cur.procs[i].depth;
        if (cur.procs[i].is_orphan)
            orphan_count++;
    }
    snprintf(detail, sizeof(detail), "%zu root%s, max depth %d", root_count,
            root_count == 1 ? "" : "s", max_depth);
    st_line("tree", cur.count == 0 || root_count > 0, detail);
    nlabels = st_collect_labels(&cur, labels, APP_ST_MAX_LABELS);
    session_count = 0;
    for (i = 0; i < cur.count; i++)
        if (cur.procs[i].is_agent_root)
            session_count++;
    if (session_count == 0)
    {
        snprintf(detail, sizeof(detail), "0 sessions, %zu orphans",
                orphan_count);
    }
    else
    {
        st_join_labels(labels, nlabels, label_ascii, sizeof(label_ascii));
        snprintf(detail, sizeof(detail), "%zu session%s (%s), %zu orphans",
                session_count, session_count == 1 ? "" : "s", label_ascii,
                orphan_count);
    }
    st_line("agent sessions", 1, detail);
    failed = !(cur.count > 0 && sys.core_count > 0 && sys.mem_total > 0
            && ports_rc == 0);
    table_free(&cur);
    plat_shutdown();
    return (failed ? 1 : 0);
}
