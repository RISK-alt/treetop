#include "process.h"

#include <string.h>

/*
** A parent must predate its child. If the process currently holding the
** parent PID was created later, that PID has been recycled and the real
** parent has exited - so the child is a root, not a child of a stranger.
*/
static t_process    *resolve_parent(t_table *tbl, t_process *child)
{
    t_process   *cand;
    size_t      i;

    if (child->ppid == 0 || child->ppid == child->key.pid)
        return (NULL);
    for (i = 0; i < tbl->count; i++)
    {
        cand = &tbl->procs[i];
        if (cand->key.pid != child->ppid)
            continue;
        if (cand->key.create_time >= child->key.create_time)
            return (NULL);
        return (cand);
    }
    return (NULL);
}

static void link_children(t_table *tbl)
{
    t_process   *p;
    t_process   *last;
    size_t      i;
    size_t      j;

    /* Append in table order so siblings keep the order the OS reported. */
    for (i = 0; i < tbl->count; i++)
    {
        p = &tbl->procs[i];
        last = NULL;
        for (j = 0; j < tbl->count; j++)
        {
            if (tbl->procs[j].parent != p)
                continue;
            if (last == NULL)
                p->first_child = &tbl->procs[j];
            else
                last->next_sibling = &tbl->procs[j];
            last = &tbl->procs[j];
        }
    }
}

/*
** depth and the two aggregate passes below are guarded on recursion depth,
** not because resolve_parent can produce a real cycle - the strict
** create_time ordering it enforces makes a pointer cycle impossible, since
** following parent links strictly decreases create_time and must end - but
** because a pathologically deep chain of legitimate processes must not blow
** the stack. Hitting the guard yields a slightly wrong tree, not a hang.
*/
static void set_depth(t_process *p, int depth, int guard)
{
    t_process   *c;

    if (guard > 512)
        return;
    p->depth = depth;
    c = p->first_child;
    while (c != NULL)
    {
        set_depth(c, depth + 1, guard + 1);
        c = c->next_sibling;
    }
}

static void aggregate(t_process *p, int guard)
{
    t_process   *c;

    if (guard > 512)
        return;
    p->subtree_cpu = p->cpu_pct;
    p->subtree_mem = p->working_set;
    p->subtree_count = 1;
    c = p->first_child;
    while (c != NULL)
    {
        aggregate(c, guard + 1);
        p->subtree_cpu += c->subtree_cpu;
        p->subtree_mem += c->subtree_mem;
        p->subtree_count += c->subtree_count;
        c = c->next_sibling;
    }
}

void    tree_build(t_table *tbl)
{
    size_t  i;

    for (i = 0; i < tbl->count; i++)
    {
        tbl->procs[i].parent = NULL;
        tbl->procs[i].first_child = NULL;
        tbl->procs[i].next_sibling = NULL;
        tbl->procs[i].depth = 0;
    }
    for (i = 0; i < tbl->count; i++)
        tbl->procs[i].parent = resolve_parent(tbl, &tbl->procs[i]);
    link_children(tbl);
    for (i = 0; i < tbl->count; i++)
    {
        if (tbl->procs[i].parent == NULL)
        {
            set_depth(&tbl->procs[i], 0, 0);
            aggregate(&tbl->procs[i], 0);
        }
    }
}

static size_t   emit(t_process *p, t_process **out, size_t max, size_t n,
                     int guard)
{
    t_process   *c;

    if (n >= max || guard > 512)
        return (n);
    out[n++] = p;
    if (p->collapsed)
        return (n);
    c = p->first_child;
    while (c != NULL && n < max)
    {
        n = emit(c, out, max, n, guard + 1);
        c = c->next_sibling;
    }
    return (n);
}

size_t  tree_flatten(t_table *tbl, t_process **out, size_t max)
{
    size_t  n;
    size_t  i;

    n = 0;
    for (i = 0; i < tbl->count && n < max; i++)
        if (tbl->procs[i].parent == NULL)
            n = emit(&tbl->procs[i], out, max, n, 0);
    return (n);
}
