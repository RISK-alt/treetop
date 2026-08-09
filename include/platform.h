#pragma once

/*                                  INCLUDES                                  */

# include "process.h"

/*                                  SYSTEM                                    */

typedef struct s_sysinfo
{
    unsigned int        core_count;
    double              cpu_pct;
    double              core_pct[TT_MAX_CORES];
    unsigned long long  mem_total;
    unsigned long long  mem_used;
    unsigned long long  uptime_secs;
}   t_sysinfo;

/*                                COLLECTORS                                  */

int                 plat_init(void);
int                 plat_limited(void);
unsigned long long  plat_now(void);
int                 plat_system(t_sysinfo *out);
int                 plat_processes(t_table *tbl);

/*
** plat_cmdline is a cache lookup, not a fresh query: the first call for
** a given key fetches from the OS and every later call for the same key
** - including one that found nothing readable - returns the same stored
** pointer. That pointer stays valid until either plat_shutdown(), or a
** plat_cmdline_sweep() call whose table no longer contains that key.
** The latter is safe precisely because (pid, create_time) identifies one
** process instance forever: once a key is missing from a freshly
** sampled table, that instance is gone and no future plat_processes()
** call can make plat_cmdline() be asked for it again. A caller that
** still needs a row's cmdline after a sweep must keep that row's own
** wcsdup'd copy, not rely on the cache past that point.
*/
const wchar_t       *plat_cmdline(t_proc_key key);
void                plat_cmdline_sweep(const t_table *tbl);

/*
** Attributes listening TCP ports from both the IPv4 and IPv6 tables.
** Returns 0 when both families were read successfully, 1 when exactly
** one was (partial - e.g. IPv6 disabled at the OS level, a real and not
** uncommon configuration, not a bug), -1 only when neither came back.
** The table is left populated with whatever was actually read in every
** case, including -1: a partial or empty result is still better than
** discarding what did succeed. Most callers only care whether this is
** non-negative; app_selftest() is the one place that distinguishes 0
** from 1, to report a partial read honestly without failing on it.
*/
int                 plat_ports(t_table *tbl);
int                 plat_kill(t_proc_key key);
void                plat_shutdown(void);
