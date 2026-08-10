#include "agent.h"

/*
** One line per agent. MATCH_CMDLINE matters because several of these run
** as node.exe with the agent's name only in argv - matching on the image
** name alone would miss them entirely.
*/
const t_agent_rule  g_agent_rules[] = {
    { L"claude",    MATCH_IMAGE | MATCH_CMDLINE, L"Claude Code" },
    { L"cursor",    MATCH_IMAGE,                 L"Cursor"      },
    { L"codex",     MATCH_IMAGE | MATCH_CMDLINE, L"Codex"       },
    { L"aider",     MATCH_IMAGE | MATCH_CMDLINE, L"Aider"       },
    { L"windsurf",  MATCH_IMAGE,                 L"Windsurf"    },
    { L"gemini",    MATCH_CMDLINE,               L"Gemini CLI"  },
    { L"opencode",  MATCH_IMAGE | MATCH_CMDLINE, L"OpenCode"    },
    { L"goose",     MATCH_IMAGE | MATCH_CMDLINE, L"Goose"       },
    { L"amp",       MATCH_CMDLINE,               L"Amp"         },
};

const size_t    g_agent_rule_count =
    sizeof(g_agent_rules) / sizeof(g_agent_rules[0]);
