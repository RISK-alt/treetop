#include "agent.h"

#include <string.h>

static wchar_t  lower(wchar_t c)
{
    return ((c >= L'A' && c <= L'Z') ? (wchar_t)(c + 32) : c);
}

/*
** Rule needles are short words ("amp", "goose", "codex", ...) and a bare
** substring search matches them inside unrelated longer words - "amp"
** inside "-steampid=", "goose" inside "mongoose". Require a delimiter (or
** the string edge) on both sides of the match so a rule fires only on a
** whole token, not a fragment of one.
*/
static int  is_left_boundary(wchar_t c)
{
    return (c == L' ' || c == L'\\' || c == L'/' || c == L'"'
            || c == L'\'' || c == L'=' || c == L':');
}

static int  is_right_boundary(wchar_t c)
{
    return (c == L' ' || c == L'\\' || c == L'/' || c == L'"'
            || c == L'\'' || c == L'.' || c == L'-' || c == L',');
}

static int  contains_ci(const wchar_t *hay, const wchar_t *needle)
{
    size_t  i;
    size_t  j;

    if (hay == NULL || needle == NULL)
        return (0);
    for (i = 0; hay[i] != L'\0'; i++)
    {
        for (j = 0; needle[j] != L'\0'; j++)
            if (lower(hay[i + j]) != lower(needle[j]))
                break;
        if (needle[j] == L'\0'
            && (i == 0 || is_left_boundary(hay[i - 1]))
            && (hay[i + j] == L'\0' || is_right_boundary(hay[i + j])))
            return (1);
    }
    return (0);
}

const t_agent_rule  *agent_match(const t_process *p)
{
    const t_agent_rule  *r;
    size_t              i;

    for (i = 0; i < g_agent_rule_count; i++)
    {
        r = &g_agent_rules[i];
        if ((r->kinds & MATCH_IMAGE) && contains_ci(p->image, r->needle))
            return (r);
        if ((r->kinds & MATCH_CMDLINE) && contains_ci(p->cmdline, r->needle))
            return (r);
    }
    return (NULL);
}

/*
** Walk down from each root. The first match owns the whole subtree, so an
** agent that shells out to another agent still reads as one session
** rather than fragmenting into several.
*/
static void descend(t_process *p, int inside, int guard)
{
    const t_agent_rule  *r;
    t_process           *c;

    if (guard > 512)
        return;
    p->is_agent_root = 0;
    p->agent_label = NULL;
    if (!inside)
    {
        r = agent_match(p);
        if (r != NULL)
        {
            p->is_agent_root = 1;
            p->agent_label = r->label;
            inside = 1;
        }
    }
    p->in_session = inside;
    c = p->first_child;
    while (c != NULL)
    {
        descend(c, inside, guard + 1);
        c = c->next_sibling;
    }
}

void    agent_classify(t_table *tbl)
{
    size_t  i;

    for (i = 0; i < tbl->count; i++)
        if (tbl->procs[i].parent == NULL)
            descend(&tbl->procs[i], 0, 0);
}
