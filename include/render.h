#pragma once

/*                                  INCLUDES                                  */

# include "platform.h"
# include <stdio.h>

/*                                 FORMATTING                                 */

void        fmt_bytes(unsigned long long bytes, wchar_t *out, size_t n);
void        fmt_duration(unsigned long long secs, wchar_t *out, size_t n);
void        fmt_shorten(const wchar_t *src, size_t max, wchar_t *out, size_t n);

/*                                   JSON                                     */

void        json_escape(const wchar_t *src, wchar_t *out, size_t n);
size_t      json_utf8_encode(const wchar_t *src, char *out, size_t n);
void        json_emit(const t_table *tbl, const t_sysinfo *sys, FILE *out);

/*                                    FRAME                                   */

/*
** A growable wide-character output buffer that one frame's worth of
** rendering is built into before a single con_write() puts it on screen.
** buf is deliberately never NUL-terminated by any of these calls - the
** length is always f->len, exactly as con_write() wants it, and a caller
** that needs a C string (tests inspecting buf directly) must terminate it
** itself.
*/
typedef struct s_frame
{
    wchar_t     *buf;
    size_t      len;
    size_t      cap;
}   t_frame;

int         frame_init(t_frame *f, size_t cap);
void        frame_free(t_frame *f);
void        frame_reset(t_frame *f);
void        frame_puts(t_frame *f, const wchar_t *s);
void        frame_printf(t_frame *f, const wchar_t *fmt, ...);
void        frame_pad(t_frame *f, int n);

/*                                   METERS                                   */

void        draw_meters(t_frame *f, const t_sysinfo *sys, int cols);
