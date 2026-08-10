#include "cli.h"
#include "treetop.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/*
** This whole file is portable C - no Win32, no getenv(). It lives in
** treetop_core specifically so treetop_tests can exercise cli_parse()
** against synthetic argv arrays. NO_COLOR is an environment read, and
** environment reads are not portable/mockable the way an argv array
** is - see cli.h's own comment on t_opts.no_color for the contract that
** keeps that one exception out of this file: src/main.c reads NO_COLOR
** and pre-sets out->no_color before ever calling cli_parse().
*/

/*                                   RANGE                                    */

/*
** Mirrors src/input/keys.c's TT_REFRESH_MIN_MS/TT_REFRESH_MAX_MS (the
** '+'/'-' key clamp) so the two ways of setting refresh_ms - the command
** line and the live keys - agree on what a valid interval is. That file
** cannot be included from here (its constants are private #defines, not
** part of a header), so the values are intentionally duplicated rather
** than shared; if one ever changes, the other must change with it.
*/
#define TT_CLI_REFRESH_MIN_MS      100u
#define TT_CLI_REFRESH_MAX_MS      60000u
#define TT_CLI_REFRESH_DEFAULT_MS  1000u

/*                                    HELP                                    */

/*
** Plain ASCII, deliberately not the Unicode glyphs the in-app help
** overlay (src/render/chrome.c's g_help_bindings) uses for the same
** bindings: that overlay only ever prints after con_init() has already
** switched the console into UTF-8/VT mode, but --help runs on a plain
** narrow stdout before any of that setup - printing raw arrow glyphs
** here would risk mangled output on a console still in its default
** code page. Spelling every binding out in words sidesteps the question
** entirely.
*/
void    cli_print_help(FILE *out)
{
    fputs("treetop - a terminal process-tree monitor\n\n", out);
    fputs("Usage: treetop [options]\n\n", out);
    fputs("Options:\n", out);
    fputs("  --refresh <ms>   refresh interval in milliseconds "
            "(100-60000, default 1000)\n", out);
    fputs("  --no-color       disable colour output "
            "(same effect as NO_COLOR)\n", out);
    fputs("  --json           print one process snapshot as JSON "
            "and exit\n", out);
    fputs("  --selftest       run internal self-checks and exit\n", out);
    fputs("  --help           show this help and exit\n", out);
    fputs("  --version        show the version and exit\n", out);
    fputs("\n", out);
    fputs("Keys:\n", out);
    fputs("  Up/Down            move selection\n", out);
    fputs("  Left/Right/Space   collapse / expand\n", out);
    fputs("  a                  agents-only view\n", out);
    fputs("  o                  orphans-only view\n", out);
    fputs("  /                  filter\n", out);
    fputs("  Esc                clear filter\n", out);
    fputs("  F5                 tree / flat view\n", out);
    fputs("  F6, <, >           sort column\n", out);
    fputs("  F9                 kill process\n", out);
    fputs("  Shift+F9           kill subtree\n", out);
    fputs("  p                  pause\n", out);
    fputs("  + / -              refresh interval\n", out);
    fputs("  F1, ?              help\n", out);
    fputs("  q                  quit\n", out);
}

/*                                  VERSION                                   */

/*
** TT_VERSION (include/treetop.h) is documented ASCII-only ("0.1.0"
** style), so a plain per-unit narrow cast is a safe, exact copy - no
** WideCharToMultiByte needed for a string this codebase already
** guarantees never carries anything above 0x7f.
*/
static void narrow_version(char *out, size_t n)
{
    size_t  i;

    i = 0;
    while (TT_VERSION[i] != L'\0' && i + 1 < n)
    {
        out[i] = (char)TT_VERSION[i];
        i++;
    }
    out[i] = '\0';
}

void    cli_print_version(FILE *out)
{
    char    ver[32];

    narrow_version(ver, sizeof(ver));
    fprintf(out, "treetop %s\n", ver);
}

/*                                  REFRESH                                   */

/*
** Accepts only a string strtol() consumes in full: no leading garbage
** (checked by end == s), no trailing garbage (checked by *end != '\0'),
** no overflow (errno == ERANGE). A leading '-' is accepted here - "-50"
** parses to -50 successfully - and rejected instead by the range check
** in cli_parse_refresh(), which already produces the "must be between
** 100 and 60000" message a negative value should get; there is no need
** for a second, differently-worded rejection path for the same input
** shape.
*/
static int  parse_long(const char *s, long *out)
{
    char    *end;

    if (s == NULL || s[0] == '\0')
        return (-1);
    errno = 0;
    *out = strtol(s, &end, 10);
    if (end == s || *end != '\0')
        return (-1);
    if (errno == ERANGE)
        return (-1);
    return (0);
}

/*
** Handles "--refresh <value>" once av[*i] itself has already matched.
** *i is advanced past the value token on success so the caller's own
** i++ lands on whatever follows; on failure *i may or may not have
** advanced, but the caller returns immediately either way so that does
** not matter.
**
** The bounds check below runs before av[*i + 1] is read anywhere, which
** is what keeps this from reading av[ac] when --refresh is the last
** argument - the exact crash the brief calls out by name.
*/
static int  cli_parse_refresh(int ac, char **av, int *i, t_opts *out,
                    FILE *err_stream)
{
    long    v;

    if (*i + 1 >= ac)
    {
        fprintf(err_stream,
                "treetop: --refresh requires a value in milliseconds\n");
        return (-1);
    }
    (*i)++;
    if (parse_long(av[*i], &v) != 0)
    {
        fprintf(err_stream, "treetop: --refresh value '%s' is not a number\n",
                av[*i]);
        return (-1);
    }
    if (v < (long)TT_CLI_REFRESH_MIN_MS || v > (long)TT_CLI_REFRESH_MAX_MS)
    {
        fprintf(err_stream,
                "treetop: --refresh must be between %u and %u ms\n",
                TT_CLI_REFRESH_MIN_MS, TT_CLI_REFRESH_MAX_MS);
        return (-1);
    }
    out->refresh_ms = (unsigned int)v;
    return (0);
}

/*                                   PARSE                                    */

int     cli_parse(int ac, char **av, t_opts *out, FILE *out_stream,
            FILE *err_stream)
{
    int i;

    out->json = 0;
    out->selftest = 0;
    out->refresh_ms = TT_CLI_REFRESH_DEFAULT_MS;
    /* out->no_color is caller-owned - see cli.h - never touched here. */
    i = 1;
    while (i < ac)
    {
        if (strcmp(av[i], "--help") == 0)
        {
            cli_print_help(out_stream);
            return (1);
        }
        else if (strcmp(av[i], "--version") == 0)
        {
            cli_print_version(out_stream);
            return (1);
        }
        else if (strcmp(av[i], "--json") == 0)
            out->json = 1;
        else if (strcmp(av[i], "--selftest") == 0)
            out->selftest = 1;
        else if (strcmp(av[i], "--no-color") == 0)
            out->no_color = 1;
        else if (strcmp(av[i], "--refresh") == 0)
        {
            if (cli_parse_refresh(ac, av, &i, out, err_stream) != 0)
                return (-1);
        }
        else
        {
            fprintf(err_stream, "treetop: unknown option '%s'\n", av[i]);
            return (-1);
        }
        i++;
    }
    return (0);
}
