#pragma once

/*                                  INCLUDES                                  */

# include "process.h"

/*                                   SORT                                     */

typedef enum e_sort_mode
{
    SORT_PID,
    SORT_CPU,
    SORT_MEM,
    SORT_NAME
}   t_sort_mode;

/*                                   VIEW                                     */

typedef struct s_view
{
    t_sort_mode     sort;
    int             sort_desc;
    int             tree_mode;
    int             agents_only;
    int             orphans_only;
    wchar_t         filter[TT_FILTER_LEN];
}   t_view;

void        view_init(t_view *v);
int         filter_match(const t_process *p, const wchar_t *needle);
size_t      view_flatten(t_table *tbl, const t_view *v,
                t_process **out, size_t max);
void        sort_rows(t_process **rows, size_t n, t_sort_mode mode, int desc);
int         cmp_rows(const t_process *a, const t_process *b,
                t_sort_mode mode, int desc);
