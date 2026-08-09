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
const wchar_t       *plat_cmdline(t_proc_key key);
int                 plat_ports(t_table *tbl);
int                 plat_kill(t_proc_key key);
void                plat_shutdown(void);
