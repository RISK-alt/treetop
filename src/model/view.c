#include "input.h"

#include <string.h>

void    view_init(t_view *v)
{
    memset(v, 0, sizeof(*v));
    v->sort = SORT_CPU;
    v->sort_desc = 1;
    v->tree_mode = 1;
}

/*
** Does this row pass every active view filter on its own merits?
**
** agents_only keeps the whole session subtree, not only its root: the
** view exists to answer "what did my agent spawn", and a root with its
** children hidden answers nothing. Orphans ride along because they are
** the other half of the same question - what it spawned and forgot.
*/
static int  row_visible(const t_process *p, const t_view *v)
{
    if (v->orphans_only && !p->is_orphan)
        return (0);
    if (v->agents_only && !p->in_session && !p->is_orphan)
        return (0);
    return (filter_match(p, v->filter));
}

/*
** A subtree is kept when anything inside it matches. Filtering a tree
** down to only the matching rows throws away the parent context that is
** the whole reason for showing a tree.
*/
static int  subtree_matches(const t_process *p, const t_view *v, int guard)
{
    const t_process *c;

    if (guard > 512)
        return (0);
    if (row_visible(p, v))
        return (1);
    c = p->first_child;
    while (c != NULL)
    {
        if (subtree_matches(c, v, guard + 1))
            return (1);
        c = c->next_sibling;
    }
    return (0);
}

/*
** Sorting in tree mode means ordering siblings, so the sort column stays
** meaningful without the hierarchy being destroyed. Re-linking the list
** keeps the tree the single source of row order.
*/
static t_process    *sort_siblings(t_process *head, const t_view *v)
{
    t_process   *sorted;
    t_process   *cur;
    t_process   *next;
    t_process   **slot;

    sorted = NULL;
    cur = head;
    while (cur != NULL)
    {
        next = cur->next_sibling;
        slot = &sorted;
        while (*slot != NULL
            && cmp_rows(*slot, cur, v->sort, v->sort_desc) <= 0)
            slot = &(*slot)->next_sibling;
        cur->next_sibling = *slot;
        *slot = cur;
        cur = next;
    }
    return (sorted);
}

static void tree_sort(t_process *p, const t_view *v, int guard)
{
    t_process   *c;

    if (guard > 512)
        return;
    p->first_child = sort_siblings(p->first_child, v);
    c = p->first_child;
    while (c != NULL)
    {
        tree_sort(c, v, guard + 1);
        c = c->next_sibling;
    }
}

static size_t   emit(t_process *p, const t_view *v, t_process **out,
                     size_t max, size_t n, int guard)
{
    t_process   *c;

    if (n >= max || guard > 512)
        return (n);
    if (!subtree_matches(p, v, 0))
        return (n);
    /*
    ** Depth is reasserted here rather than trusted from tree_build,
    ** because flatten_flat zeroes it on the shared table. Without this,
    ** switching to flat mode and back within one sample renders the
    ** whole tree unindented until the next refresh.
    */
    p->depth = guard;
    out[n++] = p;
    if (p->collapsed)
        return (n);
    c = p->first_child;
    while (c != NULL && n < max)
    {
        n = emit(c, v, out, max, n, guard + 1);
        c = c->next_sibling;
    }
    return (n);
}

static size_t   flatten_flat(t_table *tbl, const t_view *v,
                             t_process **out, size_t max)
{
    t_process   *p;
    size_t      n;
    size_t      i;

    n = 0;
    for (i = 0; i < tbl->count && n < max; i++)
    {
        p = &tbl->procs[i];
        if (!row_visible(p, v))
            continue;
        p->depth = 0;
        out[n++] = p;
    }
    sort_rows(out, n, v->sort, v->sort_desc);
    return (n);
}

/*
** Roots are not a linked list, so they are gathered and sorted in a
** scratch array. A Windows machine has a handful of true roots; the cap
** is far above anything real and simply truncates rather than failing.
** Both loops below are bounded on nroots (not merely tbl->count), so
** neither the gather nor the sort can ever write past roots[].
*/
#define VIEW_MAX_ROOTS 1024

static size_t   flatten_tree(t_table *tbl, const t_view *v,
                             t_process **out, size_t max)
{
    t_process   *roots[VIEW_MAX_ROOTS];
    size_t      nroots;
    size_t      n;
    size_t      i;

    nroots = 0;
    for (i = 0; i < tbl->count && nroots < VIEW_MAX_ROOTS; i++)
        if (tbl->procs[i].parent == NULL)
            roots[nroots++] = &tbl->procs[i];
    for (i = 0; i < nroots; i++)
        tree_sort(roots[i], v, 0);
    sort_rows(roots, nroots, v->sort, v->sort_desc);
    n = 0;
    for (i = 0; i < nroots && n < max; i++)
        n = emit(roots[i], v, out, max, n, 0);
    return (n);
}

size_t  view_flatten(t_table *tbl, const t_view *v,
                     t_process **out, size_t max)
{
    if (!v->tree_mode)
        return (flatten_flat(tbl, v, out, max));
    return (flatten_tree(tbl, v, out, max));
}
