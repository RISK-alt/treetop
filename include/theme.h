#pragma once

/*                                  INCLUDES                                  */

# include <wchar.h>

/*                                COLOUR SWITCH                               */

/*
** 0 disables every escape sequence theme_load() emits; 1 (the default)
** leaves them on. theme_load() checks this flag in exactly one place -
** itself - so a caller never has to remember to. That is what keeps
** --no-color (Task 21) from becoming a scattered set of `if (g_color)`
** guards as more render code lands in Tasks 17 and 18.
*/
extern int  g_color;

/*                                   PALETTE                                  */

/*
** Structural markers, unrelated to load: accent for agent session roots,
** dim for paths and arguments, reset to end any run started above, invert
** for the selected row, and a fixed red for orphans. The one rule for
** colour is that hue encodes load and nothing else - these five are the
** deliberate, rare exceptions the design calls out (an accent hue and one
** fixed alarm colour), everything else in this palette is a plain
** attribute (dim, invert) rather than a hue.
*/
# define TT_ACCENT      L"\x1b[36m"
# define TT_DIM         L"\x1b[2m"
# define TT_RESET       L"\x1b[0m"
# define TT_INVERT      L"\x1b[7m"
# define TT_ORPHAN      L"\x1b[31m"

/*                                 LOAD RAMP                                  */

/*
** The single green -> yellow-green -> amber -> red ramp that drives both
** the CPU/memory gauges and, later, the CPU% column - one scale, so
** peripheral vision alone finds what is hot anywhere on screen.
*/
const wchar_t   *theme_load(double pct);
