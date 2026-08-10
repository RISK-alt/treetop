#include "process.h"

static const t_process  *find_prev(const t_table *prev, t_proc_key key)
{
    size_t  i;

    for (i = 0; i < prev->count; i++)
        if (key_eq(prev->procs[i].key, key))
            return (&prev->procs[i]);
    return (NULL);
}

/*
** cpu_pct = (dkernel + duser) / (dwall * cores) * 100
**
** Normalised across every logical core, so a saturated machine reads
** 100 % the way Task Manager does rather than htop's per-core scale.
*/
void    delta_apply(t_table *cur, const t_table *prev)
{
    const t_process     *old;
    t_process           *p;
    unsigned long long  dwall;
    unsigned long long  busy;
    size_t              i;
    double              denom;

    dwall = 0;
    if (prev != NULL && cur->sample_time > prev->sample_time)
        dwall = cur->sample_time - prev->sample_time;
    for (i = 0; i < cur->count; i++)
    {
        p = &cur->procs[i];
        p->cpu_pct = 0.0;
        p->mem_pct = 0.0;
        if (cur->mem_total > 0)
            p->mem_pct = (double)p->working_set * 100.0
                / (double)cur->mem_total;
        if (dwall == 0 || cur->core_count == 0)
            continue;
        old = find_prev(prev, p->key);
        if (old == NULL)
            continue;
        /* Cumulative counters only ever grow; anything else is a glitch. */
        if (p->kernel_time < old->kernel_time || p->user_time < old->user_time)
            continue;
        busy = (p->kernel_time - old->kernel_time)
             + (p->user_time - old->user_time);
        denom = (double)dwall * (double)cur->core_count;
        p->cpu_pct = (double)busy * 100.0 / denom;
        if (p->cpu_pct > 100.0)
            p->cpu_pct = 100.0;
    }
}
