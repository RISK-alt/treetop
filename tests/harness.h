#pragma once

# include <stdio.h>
# include <string.h>
# include <wchar.h>

extern int  g_tt_run;
extern int  g_tt_fail;

# define TT_CHECK(cond)                                                       \
    do {                                                                      \
        g_tt_run++;                                                           \
        if (!(cond)) {                                                        \
            g_tt_fail++;                                                      \
            fprintf(stderr, "  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                                     \
    } while (0)

# define TT_EQ_INT(a, b)                                                      \
    do {                                                                      \
        long long _a = (long long)(a), _b = (long long)(b);                   \
        g_tt_run++;                                                           \
        if (_a != _b) {                                                       \
            g_tt_fail++;                                                      \
            fprintf(stderr, "  FAIL %s:%d  %s == %s  (%lld vs %lld)\n",       \
                    __FILE__, __LINE__, #a, #b, _a, _b);                      \
        }                                                                     \
    } while (0)

# define TT_EQ_DBL(a, b, eps)                                                 \
    do {                                                                      \
        double _a = (double)(a), _b = (double)(b);                            \
        g_tt_run++;                                                           \
        if ((_a - _b) > (eps) || (_b - _a) > (eps)) {                         \
            g_tt_fail++;                                                      \
            fprintf(stderr, "  FAIL %s:%d  %s == %s  (%f vs %f)\n",           \
                    __FILE__, __LINE__, #a, #b, _a, _b);                      \
        }                                                                     \
    } while (0)

# define TT_EQ_WSTR(a, b)                                                     \
    do {                                                                      \
        g_tt_run++;                                                           \
        if (wcscmp((a), (b)) != 0) {                                          \
            g_tt_fail++;                                                      \
            fprintf(stderr, "  FAIL %s:%d  %s == %s  (\"%ls\" vs \"%ls\")\n", \
                    __FILE__, __LINE__, #a, #b, (a), (b));                    \
        }                                                                     \
    } while (0)
