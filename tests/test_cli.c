/* tests/test_cli.c */
#include "harness.h"
#include "cli.h"

#include <stdlib.h>
#include <string.h>

/*
** Reads back everything written to a tmpfile() so far, NUL-terminated.
** cli_print_help()/cli_print_version() take a FILE* precisely so tests
** can do this instead of hijacking the process's real stdout.
*/
static void read_back(FILE *fp, char *buf, size_t n)
{
    size_t  got;

    rewind(fp);
    got = fread(buf, 1, n - 1, fp);
    buf[got] = '\0';
}

/*                                   FLAGS                                    */

static void test_json_flag(void)
{
    char    *av[2];
    t_opts  o;
    int     rc;

    av[0] = "treetop";
    av[1] = "--json";
    o.no_color = 0;
    rc = cli_parse(2, av, &o);
    TT_EQ_INT(rc, 0);
    TT_EQ_INT(o.json, 1);
    TT_EQ_INT(o.selftest, 0);
}

static void test_selftest_flag(void)
{
    char    *av[2];
    t_opts  o;
    int     rc;

    av[0] = "treetop";
    av[1] = "--selftest";
    o.no_color = 0;
    rc = cli_parse(2, av, &o);
    TT_EQ_INT(rc, 0);
    TT_EQ_INT(o.selftest, 1);
    TT_EQ_INT(o.json, 0);
}

static void test_no_flags_defaults(void)
{
    char    *av[1];
    t_opts  o;
    int     rc;

    av[0] = "treetop";
    o.no_color = 0;
    rc = cli_parse(1, av, &o);
    TT_EQ_INT(rc, 0);
    TT_EQ_INT(o.json, 0);
    TT_EQ_INT(o.selftest, 0);
    TT_EQ_INT(o.no_color, 0);
    TT_EQ_INT((int)o.refresh_ms, 1000);
}

/*                                  REFRESH                                   */

static void test_refresh_sets_value(void)
{
    char    *av[3];
    t_opts  o;
    int     rc;

    av[0] = "treetop";
    av[1] = "--refresh";
    av[2] = "5000";
    o.no_color = 0;
    rc = cli_parse(3, av, &o);
    TT_EQ_INT(rc, 0);
    TT_EQ_INT((int)o.refresh_ms, 5000);
}

/*
** Boundary tests, not a mid-range value: 100 and 60000 are the extreme
** ends of the accepted range and must both be admitted, while 99 and
** 60001 - one step outside on either side - must both be refused.
*/
static void test_refresh_boundary_100_is_accepted(void)
{
    char    *av[3];
    t_opts  o;
    int     rc;

    av[0] = "treetop";
    av[1] = "--refresh";
    av[2] = "100";
    o.no_color = 0;
    rc = cli_parse(3, av, &o);
    TT_EQ_INT(rc, 0);
    TT_EQ_INT((int)o.refresh_ms, 100);
}

static void test_refresh_boundary_60000_is_accepted(void)
{
    char    *av[3];
    t_opts  o;
    int     rc;

    av[0] = "treetop";
    av[1] = "--refresh";
    av[2] = "60000";
    o.no_color = 0;
    rc = cli_parse(3, av, &o);
    TT_EQ_INT(rc, 0);
    TT_EQ_INT((int)o.refresh_ms, 60000);
}

static void test_refresh_boundary_99_is_rejected(void)
{
    char    *av[3];
    t_opts  o;
    int     rc;

    av[0] = "treetop";
    av[1] = "--refresh";
    av[2] = "99";
    o.no_color = 0;
    rc = cli_parse(3, av, &o);
    TT_EQ_INT(rc, -1);
}

static void test_refresh_boundary_60001_is_rejected(void)
{
    char    *av[3];
    t_opts  o;
    int     rc;

    av[0] = "treetop";
    av[1] = "--refresh";
    av[2] = "60001";
    o.no_color = 0;
    rc = cli_parse(3, av, &o);
    TT_EQ_INT(rc, -1);
}

static void test_refresh_non_numeric_is_rejected(void)
{
    char    *av[3];
    t_opts  o;
    int     rc;

    av[0] = "treetop";
    av[1] = "--refresh";
    av[2] = "soon";
    o.no_color = 0;
    rc = cli_parse(3, av, &o);
    TT_EQ_INT(rc, -1);
}

static void test_refresh_negative_is_rejected(void)
{
    char    *av[3];
    t_opts  o;
    int     rc;

    av[0] = "treetop";
    av[1] = "--refresh";
    av[2] = "-50";
    o.no_color = 0;
    rc = cli_parse(3, av, &o);
    TT_EQ_INT(rc, -1);
}

/*
** A value that looks like another flag must still be rejected as "not a
** number", not silently swallowed as the value and not misread as a
** second, unrelated flag.
*/
static void test_refresh_flag_shaped_value_is_rejected(void)
{
    char    *av[3];
    t_opts  o;
    int     rc;

    av[0] = "treetop";
    av[1] = "--refresh";
    av[2] = "--json";
    o.no_color = 0;
    rc = cli_parse(3, av, &o);
    TT_EQ_INT(rc, -1);
}

/*
** --refresh as the very last argument, with no value following, is the
** crash risk the brief calls out by name: av is allocated with EXACTLY
** ac elements (no extra NULL slot beyond argv[argc-1], unlike a real
** argv from the CRT), so any read of av[ac] is a read past the end of
** this allocation. cli_parse must check bounds before touching it,
** returning -1 instead of reading past av[ac-1].
*/
static void test_refresh_missing_value_does_not_read_past_av(void)
{
    char    **av;
    t_opts  o;
    int     rc;

    av = malloc(sizeof(char *) * 2);
    TT_CHECK(av != NULL);
    if (av == NULL)
        return ;
    av[0] = "treetop";
    av[1] = "--refresh";
    o.no_color = 0;
    rc = cli_parse(2, av, &o);
    TT_EQ_INT(rc, -1);
    free(av);
}

/*                                  UNKNOWN                                   */

static void test_unknown_flag_is_rejected(void)
{
    char    *av[2];
    t_opts  o;
    int     rc;

    av[0] = "treetop";
    av[1] = "--bogus";
    o.no_color = 0;
    rc = cli_parse(2, av, &o);
    TT_EQ_INT(rc, -1);
}

/*                                HELP/VERSION                                */

/*
** --help must stop parsing immediately - a flag placed after it on the
** command line must never take effect, which is what "does not run the
** tool" means in practice: nothing past --help is even looked at.
*/
static void test_help_returns_1_and_stops_parsing(void)
{
    char    *av[3];
    t_opts  o;
    int     rc;

    av[0] = "treetop";
    av[1] = "--help";
    av[2] = "--json";
    o.no_color = 0;
    o.json = 0;
    rc = cli_parse(3, av, &o);
    TT_EQ_INT(rc, 1);
    TT_EQ_INT(o.json, 0);
}

static void test_version_returns_1(void)
{
    char    *av[2];
    t_opts  o;
    int     rc;

    av[0] = "treetop";
    av[1] = "--version";
    o.no_color = 0;
    rc = cli_parse(2, av, &o);
    TT_EQ_INT(rc, 1);
}

static void test_help_output_lists_every_flag_and_binding(void)
{
    FILE    *fp;
    char    buf[4096];

    fp = tmpfile();
    TT_CHECK(fp != NULL);
    if (fp == NULL)
        return ;
    cli_print_help(fp);
    read_back(fp, buf, sizeof(buf));
    fclose(fp);
    /* every flag */
    TT_CHECK(strstr(buf, "--refresh") != NULL);
    TT_CHECK(strstr(buf, "--no-color") != NULL);
    TT_CHECK(strstr(buf, "--json") != NULL);
    TT_CHECK(strstr(buf, "--selftest") != NULL);
    TT_CHECK(strstr(buf, "--help") != NULL);
    TT_CHECK(strstr(buf, "--version") != NULL);
    /* every key binding */
    TT_CHECK(strstr(buf, "Up/Down") != NULL);
    TT_CHECK(strstr(buf, "Left/Right") != NULL);
    TT_CHECK(strstr(buf, "Space") != NULL);
    TT_CHECK(strstr(buf, "collapse") != NULL);
    TT_CHECK(strstr(buf, "agents-only") != NULL);
    TT_CHECK(strstr(buf, "orphans-only") != NULL);
    TT_CHECK(strstr(buf, "filter") != NULL);
    TT_CHECK(strstr(buf, "Esc") != NULL);
    TT_CHECK(strstr(buf, "F5") != NULL);
    TT_CHECK(strstr(buf, "F6") != NULL);
    TT_CHECK(strstr(buf, "<") != NULL);
    TT_CHECK(strstr(buf, ">") != NULL);
    TT_CHECK(strstr(buf, "F9") != NULL);
    TT_CHECK(strstr(buf, "Shift+F9") != NULL);
    TT_CHECK(strstr(buf, "pause") != NULL);
    TT_CHECK(strstr(buf, "F1") != NULL);
    TT_CHECK(strstr(buf, "?") != NULL);
    TT_CHECK(strstr(buf, "quit") != NULL);
}

static void test_version_output_contains_version_string(void)
{
    FILE    *fp;
    char    buf[256];

    fp = tmpfile();
    TT_CHECK(fp != NULL);
    if (fp == NULL)
        return ;
    cli_print_version(fp);
    read_back(fp, buf, sizeof(buf));
    fclose(fp);
    TT_CHECK(strstr(buf, "0.1.0") != NULL);
    TT_CHECK(strstr(buf, "treetop") != NULL);
}

/*                                  NO_COLOR                                  */

/*
** cli_parse() never reads the environment itself (see cli.h); the
** caller pre-sets out->no_color from NO_COLOR before calling. These
** tests simulate both states of that pre-set and confirm cli_parse()
** treats it exactly like --no-color: settable to 1, never resettable to
** 0. There is deliberately no --color flag to test "turning it back on"
** against - the whole point is that no such path exists.
*/
static void test_no_color_flag_sets_it(void)
{
    char    *av[2];
    t_opts  o;
    int     rc;

    av[0] = "treetop";
    av[1] = "--no-color";
    o.no_color = 0;
    rc = cli_parse(2, av, &o);
    TT_EQ_INT(rc, 0);
    TT_EQ_INT(o.no_color, 1);
}

static void test_no_color_env_preset_survives_with_no_flag(void)
{
    char    *av[1];
    t_opts  o;
    int     rc;

    av[0] = "treetop";
    o.no_color = 1;
    rc = cli_parse(1, av, &o);
    TT_EQ_INT(rc, 0);
    TT_EQ_INT(o.no_color, 1);
}

static void test_no_color_flag_does_not_clear_env_preset(void)
{
    char    *av[2];
    t_opts  o;
    int     rc;

    av[0] = "treetop";
    av[1] = "--no-color";
    o.no_color = 1;
    rc = cli_parse(2, av, &o);
    TT_EQ_INT(rc, 0);
    TT_EQ_INT(o.no_color, 1);
}

/*
** Absence of the flag, with no env preset either, must never turn
** colour off by itself - a plain run stays coloured.
*/
static void test_no_flag_no_env_leaves_color_on(void)
{
    char    *av[1];
    t_opts  o;
    int     rc;

    av[0] = "treetop";
    o.no_color = 0;
    rc = cli_parse(1, av, &o);
    TT_EQ_INT(rc, 0);
    TT_EQ_INT(o.no_color, 0);
}

void    test_cli(void)
{
    test_json_flag();
    test_selftest_flag();
    test_no_flags_defaults();
    test_refresh_sets_value();
    test_refresh_boundary_100_is_accepted();
    test_refresh_boundary_60000_is_accepted();
    test_refresh_boundary_99_is_rejected();
    test_refresh_boundary_60001_is_rejected();
    test_refresh_non_numeric_is_rejected();
    test_refresh_negative_is_rejected();
    test_refresh_flag_shaped_value_is_rejected();
    test_refresh_missing_value_does_not_read_past_av();
    test_unknown_flag_is_rejected();
    test_help_returns_1_and_stops_parsing();
    test_version_returns_1();
    test_help_output_lists_every_flag_and_binding();
    test_version_output_contains_version_string();
    test_no_color_flag_sets_it();
    test_no_color_env_preset_survives_with_no_flag();
    test_no_color_flag_does_not_clear_env_preset();
    test_no_flag_no_env_leaves_color_on();
}
