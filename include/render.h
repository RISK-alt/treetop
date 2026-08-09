#pragma once

/*                                  INCLUDES                                  */

# include "treetop.h"

/*                                 FORMATTING                                 */

void        fmt_bytes(unsigned long long bytes, wchar_t *out, size_t n);
void        fmt_duration(unsigned long long secs, wchar_t *out, size_t n);
void        fmt_shorten(const wchar_t *src, size_t max, wchar_t *out, size_t n);
