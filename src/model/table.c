#include "process.h"

#include <stdlib.h>
#include <string.h>

int     key_eq(t_proc_key a, t_proc_key b)
{
    return (a.pid == b.pid && a.create_time == b.create_time);
}

int     table_init(t_table *tbl, size_t capacity)
{
    memset(tbl, 0, sizeof(*tbl));
    if (capacity == 0)
        capacity = 256;
    tbl->procs = calloc(capacity, sizeof(t_process));
    if (tbl->procs == NULL)
        return (-1);
    tbl->capacity = capacity;
    return (0);
}

void    table_free(t_table *tbl)
{
    free(tbl->procs);
    tbl->procs = NULL;
    tbl->count = 0;
    tbl->capacity = 0;
}

void    table_clear(t_table *tbl)
{
    tbl->count = 0;
}

/*
** Reallocation invalidates every t_process pointer handed out earlier.
** Callers must finish adding before tree_build() links parents and
** children, and must never hold a t_process * across an add.
*/
t_process   *table_add(t_table *tbl, const t_process *src)
{
    t_process   *grown;
    size_t      cap;

    if (tbl->count == tbl->capacity)
    {
        cap = tbl->capacity * 2;
        grown = realloc(tbl->procs, cap * sizeof(t_process));
        if (grown == NULL)
            return (NULL);
        memset(grown + tbl->capacity, 0,
               (cap - tbl->capacity) * sizeof(t_process));
        tbl->procs = grown;
        tbl->capacity = cap;
    }
    tbl->procs[tbl->count] = *src;
    return (&tbl->procs[tbl->count++]);
}

t_process   *table_find(t_table *tbl, t_proc_key key)
{
    size_t  i;

    for (i = 0; i < tbl->count; i++)
        if (key_eq(tbl->procs[i].key, key))
            return (&tbl->procs[i]);
    return (NULL);
}

t_process   *table_find_pid(t_table *tbl, unsigned long pid)
{
    size_t  i;

    for (i = 0; i < tbl->count; i++)
        if (tbl->procs[i].key.pid == pid)
            return (&tbl->procs[i]);
    return (NULL);
}
