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
    tree_mark_orphans(tbl);
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

/*                              SUBTREE COLLECTION                            */

/*
** guard bounds recursion depth exactly like set_depth/aggregate above, for
** the same reason: tree_build's strict create_time ordering already makes
** a real parent/child cycle impossible, this is only insurance against a
** pathologically deep legitimate chain blowing the stack. Hitting the
** guard truncates the walk (root itself may end up missing from a
** sufficiently deep, sufficiently pathological tree) rather than hanging.
*/
static size_t   collect_deepest(t_process *p, t_process **out, size_t max,
                    size_t n, int guard)
{
    t_process   *c;

    if (guard > 512)
        return (n);
    c = p->first_child;
    while (c != NULL)
    {
        n = collect_deepest(c, out, max, n, guard + 1);
        c = c->next_sibling;
    }
    if (n < max)
        out[n++] = p;
    return (n);
}

size_t  tree_collect_subtree(t_process *root, t_process **out, size_t max)
{
    if (root == NULL)
        return (0);
    return (collect_deepest(root, out, max, 0, 0));
}

/*                                 ORPHANS                                    */

/*
** Processes whose parent has exited are extremely common on Windows -
** anything launched from a terminal that has since closed. Only the ones
** that look like a tool left something running are worth flagging.
*/
static const wchar_t    *g_dev_runtimes[] = {
    L"node", L"deno", L"bun", L"python", L"pythonw", L"cargo", L"rustc",
    L"dotnet", L"java", L"go", L"pwsh", L"powershell", L"cmd", L"npm",
    L"pnpm", L"yarn", L"vite", L"webpack", L"esbuild", L"tsc", L"nodemon",
    NULL
};

/*
** The boot and session chain Windows legitimately re-parents: smss exits
** after handing off to wininit/winlogon, and the rest are core system
** components that routinely end up parentless without any tool or agent
** being involved. Flagging these as orphans would teach the user to
** distrust the ORPHAN marker, which is worse than missing a real one.
*/
static const wchar_t    *g_system_images[] = {
    L"system", L"registry", L"smss", L"csrss", L"wininit", L"winlogon",
    L"services", L"lsass", L"svchost", L"fontdrvhost", L"dwm", L"spoolsv",
    L"sihost", L"taskhostw", L"ctfmon", L"runtimebroker", L"searchindexer",
    L"wudfhost", L"audiodg", L"conhost", L"dllhost", L"wmiprvse",
    NULL
};

static int  wcs_ieq(const wchar_t *a, const wchar_t *b)
{
    while (*a != L'\0' && *b != L'\0')
    {
        wchar_t ca = (*a >= L'A' && *a <= L'Z') ? (wchar_t)(*a + 32) : *a;
        wchar_t cb = (*b >= L'A' && *b <= L'Z') ? (wchar_t)(*b + 32) : *b;

        if (ca != cb)
            return (0);
        a++;
        b++;
    }
    return (*a == *b);
}

/*
** Shared by is_dev_runtime and is_system_image: extract the extension-free
** stem once, then check it against a NULL-terminated name list.
*/
static int  matches_stem_list(const wchar_t *image, const wchar_t *const *list)
{
    wchar_t         stem[TT_IMAGE_LEN];
    const wchar_t   *dot;
    size_t          len;
    int             i;

    if (image == NULL || image[0] == L'\0')
        return (0);
    dot = wcsrchr(image, L'.');
    len = (dot != NULL) ? (size_t)(dot - image) : wcslen(image);
    if (len >= TT_IMAGE_LEN)
        len = TT_IMAGE_LEN - 1;
    wmemcpy(stem, image, len);
    stem[len] = L'\0';
    for (i = 0; list[i] != NULL; i++)
        if (wcs_ieq(stem, list[i]))
            return (1);
    return (0);
}

int     is_dev_runtime(const wchar_t *image)
{
    return (matches_stem_list(image, g_dev_runtimes));
}

int     is_system_image(const wchar_t *image)
{
    return (matches_stem_list(image, g_system_images));
}

void    tree_mark_orphans(t_table *tbl)
{
    t_process   *p;
    size_t      i;

    for (i = 0; i < tbl->count; i++)
    {
        p = &tbl->procs[i];
        p->is_orphan = 0;
        if (p->parent != NULL || p->ppid == 0)
            continue;
        if (is_system_image(p->image))
            continue;
        if (p->port_count > 0 || is_dev_runtime(p->image))
            p->is_orphan = 1;
    }
}
