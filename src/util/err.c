#include "treetop.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void    tt_warn(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fputs("treetop: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void    tt_fatal(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fputs("treetop: fatal: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}
