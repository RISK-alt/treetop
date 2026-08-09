#include "treetop.h"
#include "app.h"
#include "render.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
** No rendering, no key handling, no terminal setup yet - Tasks 15-19
** build those. This is the flat PID/CPU%/MEM/COMMAND dump that proves
** the sampling pipeline actually works end to end before anything gets
** built on top of it.
*/
static int  run_flat(void)
{
    t_app       a;
    t_process   **rows;
    size_t      i;
    wchar_t     membuf[32];
    const wchar_t *cmd;

    if (app_init(&a) != 0)
    {
        tt_warn("failed to initialise");
        return (1);
    }
    if (a.limited)
        tt_warn("running in limited mode (ntdll unavailable)");
    /*
    ** cpu_pct is a delta between two samples; a single sample has no
    ** previous tick to diff against, so every row would read 0.0 without
    ** this second call. The sleep in between is the refresh interval
    ** itself, not an arbitrary pause.
    */
    app_sample(&a);
    Sleep(a.refresh_ms);
    app_sample(&a);
    rows = malloc(a.cur.count * sizeof(t_process *));
    if (rows == NULL)
    {
        tt_warn("out of memory");
        app_free(&a);
        return (1);
    }
    for (i = 0; i < a.cur.count; i++)
        rows[i] = &a.cur.procs[i];
    sort_rows(rows, a.cur.count, SORT_CPU, 1);
    printf("%7s %6s %8s  %s\n", "PID", "CPU%", "MEM", "COMMAND");
    for (i = 0; i < a.cur.count; i++)
    {
        fmt_bytes(rows[i]->working_set, membuf, 32);
        cmd = rows[i]->cmdline;
        if (cmd == NULL || cmd[0] == L'\0')
            cmd = rows[i]->image;
        printf("%7lu %5.1f%% %8ls  %ls\n", rows[i]->key.pid,
                rows[i]->cpu_pct, membuf, cmd);
    }
    free(rows);
    app_free(&a);
    return (0);
}

/*
** --json samples twice with the refresh interval in between, for the
** same reason run_flat() does: cpu_pct is a delta between two samples,
** so a single sample would report zero for every process and the whole
** point of this flag - letting an agent see its own real footprint -
** would be a lie.
*/
static int  run_json(void)
{
    t_app   a;

    if (app_init(&a) != 0)
    {
        tt_warn("failed to initialise");
        return (1);
    }
    if (a.limited)
        tt_warn("running in limited mode (ntdll unavailable)");
    app_sample(&a);
    Sleep(a.refresh_ms);
    app_sample(&a);
    json_emit(&a.cur, &a.sys, stdout);
    app_free(&a);
    return (0);
}

int     main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--selftest") == 0)
        return (app_selftest());
    if (argc > 1 && strcmp(argv[1], "--json") == 0)
        return (run_json());
    return (run_flat());
}
