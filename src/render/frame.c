#include "render.h"

#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>

/*
** This file is portable C - no Win32 - so it lives in treetop_core and
** treetop_tests can exercise it without a live console.
*/

/*                                   GROWTH                                   */

/*
** Grows f->buf so at least `extra` more wchar_t fit after f->len,
** doubling capacity until it does. Returns 0 and leaves f completely
** untouched (buf, len and cap all unchanged) if realloc fails, so a
** caller that checks the return value never has to unwind a partial
** write: nothing was written yet at the point this can fail.
*/
static int  frame_ensure(t_frame *f, size_t extra)
{
    size_t  want;
    size_t  newcap;
    wchar_t *nb;

    want = f->len + extra;
    if (want <= f->cap)
        return (1);
    newcap = f->cap;
    if (newcap == 0)
        newcap = 16;
    while (newcap < want)
    {
        if (newcap > (size_t)-1 / 2 / sizeof(wchar_t))
            return (0);
        newcap *= 2;
    }
    nb = realloc(f->buf, newcap * sizeof(wchar_t));
    if (nb == NULL)
        return (0);
    f->buf = nb;
    f->cap = newcap;
    return (1);
}

/*
** Copies exactly `len` wide characters in, growing first if needed. Used
** by both frame_puts and frame_printf so there is one place that keeps
** f->len in sync with what was actually copied - on growth failure
** nothing is written and f->len is untouched, never left pointing past
** real content.
*/
static void frame_write(t_frame *f, const wchar_t *s, size_t len)
{
    if (!frame_ensure(f, len))
        return ;
    wmemcpy(f->buf + f->len, s, len);
    f->len += len;
}

/*                                   BUFFER                                   */

int     frame_init(t_frame *f, size_t cap)
{
    if (cap == 0)
        cap = 1;
    f->buf = malloc(cap * sizeof(wchar_t));
    if (f->buf == NULL)
        return (-1);
    f->len = 0;
    f->cap = cap;
    return (0);
}

void    frame_free(t_frame *f)
{
    free(f->buf);
    f->buf = NULL;
    f->len = 0;
    f->cap = 0;
}

/*
** Rewinds the write cursor without touching buf or cap, so the next
** frame reuses the allocation instead of re-growing it from scratch
** every tick. buf keeps whatever bytes were in it past f->len - callers
** rely on len alone, per the non-NUL-terminated invariant.
*/
void    frame_reset(t_frame *f)
{
    f->len = 0;
}

void    frame_puts(t_frame *f, const wchar_t *s)
{
    frame_write(f, s, wcslen(s));
}

/*
** vswprintf's return value on Windows follows the strict ISO C rule
** (negative on truncation or error), not the glibc extension that
** reports the length that would have been needed. There is therefore no
** way to size a buffer from one failed call - this doubles a scratch
** buffer and retries until the call succeeds, rather than truncating
** silently or guessing a size.
*/
void    frame_printf(t_frame *f, const wchar_t *fmt, ...)
{
    wchar_t     stackbuf[128];
    wchar_t     *heap;
    size_t      cap;
    int         n;
    va_list     ap;

    va_start(ap, fmt);
    n = vswprintf(stackbuf, 128, fmt, ap);
    va_end(ap);
    if (n >= 0)
    {
        frame_write(f, stackbuf, (size_t)n);
        return ;
    }
    cap = 256;
    for (;;)
    {
        heap = malloc(cap * sizeof(wchar_t));
        if (heap == NULL)
            return ;
        va_start(ap, fmt);
        n = vswprintf(heap, cap, fmt, ap);
        va_end(ap);
        if (n >= 0)
        {
            frame_write(f, heap, (size_t)n);
            free(heap);
            return ;
        }
        free(heap);
        if (cap > (size_t)-1 / 2 / sizeof(wchar_t))
            return ;
        cap *= 2;
    }
}

void    frame_pad(t_frame *f, int n)
{
    int i;

    if (n <= 0)
        return ;
    if (!frame_ensure(f, (size_t)n))
        return ;
    i = 0;
    while (i < n)
    {
        f->buf[f->len + (size_t)i] = L' ';
        i++;
    }
    f->len += (size_t)n;
}
