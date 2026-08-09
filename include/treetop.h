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

/*                                  STRINGS                                   */

/*
** ASCII-only case folding, shared by every case-insensitive matcher in
** this codebase (src/agent/detect.c's token-boundary rule matching,
** src/input/filter.c's plain substring filter, src/model/tree.c's exact
** image-name equality for the orphan classifier's static name lists) -
** three genuinely different predicates that all still need the same one
** "fold A-Z to a-z, leave everything else untouched" primitive
** underneath. ASCII-only is deliberate, not an oversight: every string
** compared through it is either a rule needle written into
** src/agent/rules.c, a static classifier name list, or arbitrary user
** filter text matched against those - none of it depends on locale-aware
** casing, and towlower() pulling in locale state is exactly the kind of
** hidden dependency a portable, unit-testable core (see design SS5) does
** not need.
*/
wchar_t     tt_lower(wchar_t c);
