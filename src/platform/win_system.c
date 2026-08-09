#include "platform.h"
#include "ntapi.h"

#include <windows.h>
#include <string.h>

/*                                  STATE                                     */

typedef struct s_core_prev
{
    unsigned long long  idle;
    unsigned long long  busy;
}   t_core_prev;

static t_core_prev  g_prev[TT_MAX_CORES];
static int          g_have_prev;
static int          g_limited;

/*                                LIFECYCLE                                   */

int     plat_init(void)
{
    g_limited = ntapi_init();
    return (g_limited);
}

int     plat_limited(void)
{
    return (g_limited);
}

/*
** plat_shutdown() is defined in win_cmdline.c, not here: it is the first
** collector that acquires a real persistent resource (the command-line
** cache's heap allocations). This file's own collectors touch only
** stack buffers and file statics, so they have nothing to release.
*/

/*                                  CLOCK                                     */

unsigned long long  plat_now(void)
{
    FILETIME        ft;
    ULARGE_INTEGER  u;

    GetSystemTimeAsFileTime(&ft);
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (u.QuadPart);
}

/*                                  MEMORY                                    */

static void read_memory(t_sysinfo *out)
{
    MEMORYSTATUSEX  ms;

    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms))
        return ;
    out->mem_total = ms.ullTotalPhys;
    out->mem_used = ms.ullTotalPhys - ms.ullAvailPhys;
}

/*                                   CPU                                      */

int     plat_system(t_sysinfo *out)
{
    SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION   perf[TT_MAX_CORES];
    SYSTEM_INFO         si;
    ULONG               len;
    ULONG               got;
    unsigned int        i;
    unsigned long long  idle;
    unsigned long long  busy;
    unsigned long long  didle;
    unsigned long long  dbusy;
    double              total;

    memset(out, 0, sizeof(*out));
    GetSystemInfo(&si);
    out->core_count = si.dwNumberOfProcessors;
    if (out->core_count > TT_MAX_CORES)
        out->core_count = TT_MAX_CORES;
    read_memory(out);
    out->uptime_secs = GetTickCount64() / 1000;
    len = 0;
    if (nt_query_system(TT_SystemProcessorPerformanceInformation, perf,
            (ULONG)(sizeof(perf[0]) * out->core_count), &len) != 0)
        return (-1);
    /*
    ** NtQuerySystemInformation can hand back fewer bytes than asked for
    ** (a processor hot-added between GetSystemInfo() above and this
    ** call, for instance). Trust only the whole entries it actually
    ** wrote - anything past `got` in `perf` is uninitialised stack
    ** memory, not a zeroed core.
    */
    got = len / (ULONG)sizeof(perf[0]);
    if (got > out->core_count)
        got = out->core_count;
    total = 0.0;
    i = 0;
    while (i < got)
    {
        /*
        ** KernelTime from this API already includes IdleTime - it is
        ** not "time spent in the kernel" by itself. Subtracting idle
        ** is what turns it into real kernel work; skip that and every
        ** machine reads as 100 % busy.
        */
        idle = (unsigned long long)perf[i].IdleTime.QuadPart;
        busy = (unsigned long long)(perf[i].KernelTime.QuadPart
             + perf[i].UserTime.QuadPart) - idle;
        if (g_have_prev && idle >= g_prev[i].idle && busy >= g_prev[i].busy)
        {
            didle = idle - g_prev[i].idle;
            dbusy = busy - g_prev[i].busy;
            if (didle + dbusy > 0)
                out->core_pct[i] = (double)dbusy * 100.0
                    / (double)(didle + dbusy);
        }
        g_prev[i].idle = idle;
        g_prev[i].busy = busy;
        total += out->core_pct[i];
        i++;
    }
    g_have_prev = 1;
    if (got > 0)
        out->cpu_pct = total / (double)got;
    return (0);
}
