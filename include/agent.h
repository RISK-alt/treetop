#pragma once

/*                                  INCLUDES                                  */

# include "process.h"

/*                                   RULES                                    */

# define MATCH_IMAGE    1
# define MATCH_CMDLINE  2

typedef struct s_agent_rule
{
    const wchar_t   *needle;
    int             kinds;
    const wchar_t   *label;
}   t_agent_rule;

extern const t_agent_rule   g_agent_rules[];
extern const size_t         g_agent_rule_count;

const t_agent_rule  *agent_match(const t_process *p);
void                agent_classify(t_table *tbl);
