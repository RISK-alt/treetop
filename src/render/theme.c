#include "theme.h"

/*
** This file is portable C - no Win32 - so it lives in treetop_core and
** treetop_tests can exercise the ramp without a live console.
*/

int     g_color = 1;

/*                                 LOAD RAMP                                  */

/*
** xterm 256-colour codes for a visibly distinct green -> yellow-green ->
** amber -> red progression. Thresholds are strictly "under": 25.0 itself
** already reads as yellow-green, 50.0 as amber, 75.0 as red, so there is
** no percentage that falls between two tiers or matches two at once.
*/
const wchar_t   *theme_load(double pct)
{
    if (!g_color)
        return (L"");
    if (pct < 25.0)
        return (L"\x1b[38;5;40m");
    if (pct < 50.0)
        return (L"\x1b[38;5;148m");
    if (pct < 75.0)
        return (L"\x1b[38;5;214m");
    return (L"\x1b[38;5;196m");
}
