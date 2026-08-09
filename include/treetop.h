#pragma once

/*                                  INCLUDES                                  */

# include <stddef.h>
# include <wchar.h>

/*                                  VERSION                                   */

# define TT_VERSION         L"0.1.0"

/*                                  LIMITS                                    */

# define TT_MAX_PORTS       8
# define TT_IMAGE_LEN       64
# define TT_FILTER_LEN      128
# define TT_MAX_CORES       128

/*                                  ERRORS                                    */

void        tt_warn(const char *fmt, ...);
void        tt_fatal(const char *fmt, ...);
