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
