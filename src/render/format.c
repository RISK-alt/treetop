#include "render.h"

#include <stdio.h>

/*
** Powers of two, one decimal below 10 and none above, which is what keeps
** the column at a fixed 5 characters.
*/
void    fmt_bytes(unsigned long long bytes, wchar_t *out, size_t n)
{
    static const wchar_t    *unit[] = { L"", L"K", L"M", L"G", L"T", L"P" };
    double                  v = (double)bytes;
    int                     u = 0;

    if (bytes < 1024)
    {
        swprintf(out, n, L"%llu", bytes);
        return;
    }
    while (v >= 1024.0 && u < 5)
    {
        v /= 1024.0;
        u++;
    }
    if (v < 10.0)
        swprintf(out, n, L"%.1f%ls", v, unit[u]);
    else
        swprintf(out, n, L"%.0f%ls", v, unit[u]);
}

void    fmt_duration(unsigned long long secs, wchar_t *out, size_t n)
{
    unsigned long long  d = secs / 86400;
    unsigned long long  h = (secs % 86400) / 3600;
    unsigned long long  m = (secs % 3600) / 60;
    unsigned long long  s = secs % 60;

    if (d > 0)
        swprintf(out, n, L"%llud %02llu:%02llu", d, h, m);
    else if (h > 0)
        swprintf(out, n, L"%llu:%02llu:%02llu", h, m, s);
    else
        swprintf(out, n, L"%llu:%02llu", m, s);
}

/*
** Long paths carry their meaning in the tail, so cut from the left and
** mark the cut with an ellipsis.
*/
void    fmt_shorten(const wchar_t *src, size_t max, wchar_t *out, size_t n)
{
    size_t  len = wcslen(src);
    size_t  keep;

    if (max == 0 || n == 0)
    {
        if (n > 0)
            out[0] = L'\0';
        return;
    }
    if (len <= max)
    {
        swprintf(out, n, L"%ls", src);
        return;
    }
    /* n is the buffer capacity and binds every branch, including this
       one: writing out[1] needs room for two wide characters. */
    if (max < 2)
    {
        if (n >= 2)
        {
            out[0] = L'…';
            out[1] = L'\0';
        }
        else
            out[0] = L'\0';
        return;
    }
    keep = max - 1;
    swprintf(out, n, L"…%ls", src + (len - keep));
}
