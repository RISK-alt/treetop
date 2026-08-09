#pragma once

/*                                  INCLUDES                                  */

# include <stdio.h>

/*                                   OPTIONS                                  */

/*
** The result of parsing argv, handed back to main() so it can decide
** which of the three run paths (interactive loop, --json, --selftest)
** to take and how to configure it.
**
** no_color is the one field cli_parse() does not unconditionally
** initialise: reading NO_COLOR from the environment is not portable and
** treetop_core must stay free of getenv() (see cli_parse()'s own
** comment), so the caller - src/main.c - reads NO_COLOR itself and sets
** out->no_color to 1 before calling cli_parse() if it was non-empty, 0
** otherwise. cli_parse() only ever sets no_color to 1, on --no-color;
** it never resets it back to 0. That makes the flag and the variable
** equivalent, and means neither can turn colour back on once the other
** has turned it off - there is no --color flag, by design. Every other
** field is fully initialised by cli_parse() and needs no caller setup.
*/
typedef struct s_opts
{
    int             json;
    int             selftest;
    int             no_color;
    unsigned int    refresh_ms;
}   t_opts;

/*
** Parses argv[1..ac). Returns 0 if the caller should continue into one
** of the three run paths (out is fully populated), 1 if it already
** printed --help or --version output and the caller should exit 0
** without running anything else, or -1 if an error (unknown option,
** malformed or out-of-range --refresh) was printed to stderr and the
** caller should exit 1.
**
** Never reads av[ac] or beyond: a flag that expects a value (--refresh)
** checks there is a following argument before touching it.
*/
int         cli_parse(int ac, char **av, t_opts *out);

/*
** Split out from cli_parse() so tests can check the exact text without
** capturing real stdout: pass a tmpfile() and read it back. cli_parse()
** itself calls these with stdout when --help/--version is seen.
*/
void        cli_print_help(FILE *out);
void        cli_print_version(FILE *out);
