#include "agent.h"

/*
** Rule needles are short words ("amp", "goose", "codex", ...) and a bare
** substring search matches them inside unrelated longer words - "amp"
** inside "-steampid=", "goose" inside "mongoose". Require a delimiter (or
** the string edge) on both sides of the match so a rule fires only on a
** whole token, not a fragment of one.
**
** The two sets are not identical, and that is deliberate in one place and
** a bug in another:
**
**   - '.' is right-boundary only, on purpose. It is what makes
**     "claude.exe" match (needle followed by '.') while ".claude" does
**     NOT match with '.' as a LEFT boundary - see the real
**     ".claude\chrome\chrome-native-host.bat" cmdline this project's own
**     README screenshot was built from. That path's cmd.exe must stay
**     unmatched so the session re-roots onto the real claude.exe child
**     instead. Do not add '.' to is_left_boundary.
**
**   - ',' used to be right-boundary only, which was a real gap, not a
**     deliberate one: a comma-separated token list with no surrounding
**     spaces ("--agents=goose,claude,cursor") has "claude" preceded by
**     ',', and is_left_boundary rejected it - a false negative, the
**     mirror image of the amp-in-steampid false positive this whole
**     function exists to prevent. ',' is a separator on either side of a
**     token, symmetrically, so it belongs in both sets.
**
**   - '(' and ')' are in both sets: "Program Files (x86)" is an ordinary
**     Windows path component (it shows up in this very file's own
**     steamwebhelper test fixture below), and a needle sitting right
**     next to a parenthesis is exactly as token-delimited as one next to
**     a space.
*/
static int  is_left_boundary(wchar_t c)
{
    return (c == L' ' || c == L'\\' || c == L'/' || c == L'"'
            || c == L'\'' || c == L'=' || c == L':' || c == L','
            || c == L'(' || c == L')');
}

static int  is_right_boundary(wchar_t c)
{
    return (c == L' ' || c == L'\\' || c == L'/' || c == L'"'
            || c == L'\'' || c == L'.' || c == L'-' || c == L','
            || c == L'(' || c == L')');
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
            if (tt_lower(hay[i + j]) != tt_lower(needle[j]))
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
