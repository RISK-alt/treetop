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

/*                                  COMMAND                                   */

/*
** Windows quotes argv[0] when the path contains spaces, which is common
** ("C:\Program Files\..."); an unquoted argv[0] simply runs to the first
** space. Returns the index just past argv[0] - the closing quote when
** quoted, the first space (or the terminating NUL) otherwise - never
** reading past that NUL either way, so a string with an unterminated
** leading quote degrades to "argv[0] is the whole string" rather than
** running off the end looking for a closing quote that never comes.
*/
static size_t   argv0_end(const wchar_t *cmdline)
{
    size_t  i;

    if (cmdline[0] == L'"')
    {
        i = 1;
        while (cmdline[i] != L'\0' && cmdline[i] != L'"')
            i++;
        if (cmdline[i] == L'"')
            i++;
        return (i);
    }
    i = 0;
    while (cmdline[i] != L'\0' && cmdline[i] != L' ')
        i++;
    return (i);
}

/*
** image's basename with a trailing ".exe" (case-insensitive) removed,
** matching the design mockup's bare "node"/"rg" style. image is always
** already just a basename (TT_IMAGE_LEN, no directory component), so
** there is no path to strip here - only the extension.
*/
static void stem_name(const wchar_t *image, wchar_t *out, size_t cap)
{
    size_t  len;

    len = wcslen(image);
    if (len >= 4 && image[len - 4] == L'.'
        && (image[len - 3] == L'e' || image[len - 3] == L'E')
        && (image[len - 2] == L'x' || image[len - 2] == L'X')
        && (image[len - 1] == L'e' || image[len - 1] == L'E'))
        len -= 4;
    if (cap == 0)
        return ;
    if (len >= cap)
        len = cap - 1;
    wmemcpy(out, image, len);
    out[len] = L'\0';
}

/*
** name + " " + tail, right-truncated with a trailing ellipsis when it
** does not fit in `max` - the mirror image of fmt_shorten's leading
** ellipsis, because here the identifying token is the FIRST thing in
** the string, not the last. `keep` (the number of source characters
** kept before the ellipsis) is split between name and tail without ever
** materialising the concatenation: precision on %.*ls bounds how much
** of each source string printf reads, so neither piece needs its own
** truncated copy first.
*/
static void compose_truncated(const wchar_t *name, const wchar_t *tail,
                              size_t namelen, size_t max,
                              wchar_t *out, size_t n)
{
    size_t  keep;
    size_t  tail_keep;

    if (max < 2)
    {
        if (n >= 2)
        {
            out[0] = L'\u2026';
            out[1] = L'\0';
        }
        else if (n > 0)
            out[0] = L'\0';
        return ;
    }
    keep = max - 1;
    if (keep <= namelen)
        swprintf(out, n, L"%.*ls\u2026", (int)keep, name);
    else
    {
        tail_keep = keep - namelen - 1;
        swprintf(out, n, L"%ls %.*ls\u2026", name, (int)tail_keep, tail);
    }
}

/*
** See render.h for the rationale (right- vs left-truncation). image is
** never NULL by contract (t_process::image is a fixed array, always at
** least an empty string); cmdline is assumed non-NULL - callers with a
** NULL cmdline have nothing to parse a tail out of and fall back to
** fmt_shorten(image, ...) directly rather than calling this at all.
*/
void    fmt_command(const wchar_t *image, const wchar_t *cmdline,
                    size_t max, wchar_t *out, size_t n)
{
    wchar_t         name[TT_IMAGE_LEN];
    const wchar_t   *tail;
    size_t          end;
    size_t          namelen;
    size_t          complen;

    if (max == 0 || n == 0)
    {
        if (n > 0)
            out[0] = L'\0';
        return ;
    }
    stem_name(image, name, TT_IMAGE_LEN);
    end = argv0_end(cmdline);
    tail = cmdline + end;
    while (*tail == L' ')
        tail++;
    if (*tail == L'\0')
    {
        fmt_shorten(name, max, out, n);
        return ;
    }
    namelen = wcslen(name);
    complen = namelen + 1 + wcslen(tail);
    if (complen <= max)
    {
        swprintf(out, n, L"%ls %ls", name, tail);
        return ;
    }
    compose_truncated(name, tail, namelen, max, out, n);
}
