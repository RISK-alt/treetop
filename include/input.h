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

/*                                   KEYS                                     */

/*
** keys_handle() takes a t_app *, but t_app is defined in app.h, which
** already #includes this header (for t_view/t_sort_mode) before that
** definition runs. #including app.h back here would hit its own
** #pragma once guard mid-parse - before struct s_app exists - and leave
** this prototype naming an unknown type. A bare forward declaration of
** the struct tag is all a pointer parameter needs; app.h's typedef
** later completes it, and every real caller (src/main.c, src/input/
** keys.c, tests/test_keys.c) has both headers in scope by the time it
** actually calls this function.
**
** Returns 1 when the caller must redraw the frame, 0 when nothing
** visible changed. Sets a->running = 0 on 'q'. Mutates only *a - never
** touches the console, so it is portable and lives in treetop_core with
** the rest of the input layer, unlike the event loop itself (src/main.c,
** the only file allowed to call con_*).
*/
struct s_app;

int         keys_handle(struct s_app *a, int key);
