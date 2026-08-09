#include "harness.h"

#include <stdlib.h>
#include <string.h>

int     g_tt_run = 0;
int     g_tt_fail = 0;

typedef struct s_suite
{
    const char  *name;
    void        (*fn)(void);
}   t_suite;

void    test_smoke(void);
void    test_format(void);
void    test_table(void);
void    test_delta(void);
void    test_tree(void);
void    test_agent(void);
void    test_view(void);
void    test_json(void);
void    test_frame(void);
void    test_draw_table(void);

static const t_suite    g_suites[] = {
    { "smoke", test_smoke },
    { "format", test_format },
    { "table", test_table },
    { "delta", test_delta },
    { "tree", test_tree },
    { "agent", test_agent },
    { "view", test_view },
    { "json", test_json },
    { "frame", test_frame },
    { "draw_table", test_draw_table },
};

static const size_t     g_suite_count = sizeof(g_suites) / sizeof(g_suites[0]);

int     main(int ac, char **av)
{
    size_t  i;
    int     ran = 0;

    for (i = 0; i < g_suite_count; i++)
    {
        if (ac > 1 && strcmp(av[1], g_suites[i].name) != 0)
            continue;
        printf("== %s\n", g_suites[i].name);
        g_suites[i].fn();
        ran = 1;
    }
    if (!ran)
    {
        fprintf(stderr, "no suite matched\n");
        return (2);
    }
    printf("\n%d checks, %d failed\n", g_tt_run, g_tt_fail);
    return (g_tt_fail == 0 ? 0 : 1);
}
