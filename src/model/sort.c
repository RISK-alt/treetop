#include "input.h"

static int  cmp_field(const t_process *a, const t_process *b, t_sort_mode mode)
{
    switch (mode)
    {
        case SORT_CPU:
            if (a->cpu_pct < b->cpu_pct) return (-1);
            if (a->cpu_pct > b->cpu_pct) return (1);
            return (0);
        case SORT_MEM:
            if (a->working_set < b->working_set) return (-1);
            if (a->working_set > b->working_set) return (1);
            return (0);
        case SORT_NAME:
            return (wcscmp(a->image, b->image));
        case SORT_PID:
        default:
            if (a->key.pid < b->key.pid) return (-1);
            if (a->key.pid > b->key.pid) return (1);
            return (0);
    }
}

int     cmp_rows(const t_process *a, const t_process *b,
                 t_sort_mode mode, int desc)
{
    int c = cmp_field(a, b, mode);

    return (desc ? -c : c);
}

/*
** Insertion sort: stable, so equal rows keep the order the tree gave
** them and the display does not shuffle between frames. A few hundred
** rows makes the complexity irrelevant.
*/
void    sort_rows(t_process **rows, size_t n, t_sort_mode mode, int desc)
{
    t_process   *tmp;
    size_t      i;
    size_t      j;

    for (i = 1; i < n; i++)
    {
        tmp = rows[i];
        j = i;
        while (j > 0 && cmp_rows(rows[j - 1], tmp, mode, desc) > 0)
        {
            rows[j] = rows[j - 1];
            j--;
        }
        rows[j] = tmp;
    }
}
