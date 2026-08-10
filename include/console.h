#pragma once

/*                                  INCLUDES                                  */

# include <stddef.h>
# include <wchar.h>

/*                                 KEY CODES                                  */

/*
** Printable keystrokes pass through con_wait_key() as their own Unicode
** value, and that value is a WCHAR - at most 0xffff. Every symbolic
** code below sits above that ceiling so it can never collide with a
** real character, the same trick wide-aware terminal libraries use for
** their own KEY_* ranges. TT_KEY_SPACE is the one exception: it names
** the pass-through value a space bar keystroke already produces (32),
** it does not need a slot of its own.
*/
# define TT_KEY_RESIZE      0x10001
# define TT_KEY_UP          0x10002
# define TT_KEY_DOWN        0x10003
# define TT_KEY_LEFT        0x10004
# define TT_KEY_RIGHT       0x10005
# define TT_KEY_F1          0x10006
# define TT_KEY_F5          0x10007
# define TT_KEY_F6          0x10008
# define TT_KEY_F9          0x10009
# define TT_KEY_SHIFT_F9    0x1000a
# define TT_KEY_ESCAPE      0x1000b
# define TT_KEY_ENTER       0x1000c
# define TT_KEY_BACKSPACE   0x1000d
# define TT_KEY_SPACE       0x00020

/*                                  CONSOLE                                   */

/*
** con_init()/con_shutdown() bracket the whole TUI session: init saves
** the console's original modes, switches it into raw VT + alternate
** screen, and registers shutdown against both atexit and Ctrl+C so
** every exit path restores what it found. con_write() puts a whole
** rendered frame on screen in one WriteConsoleW; con_wait_key() is the
** input side, blocking up to timeout_ms for either a keystroke or a
** resize before reporting a timeout.
*/
int         con_init(void);
void        con_shutdown(void);
void        con_size(int *cols, int *rows);
void        con_write(const wchar_t *buf, size_t len);
int         con_wait_key(unsigned int timeout_ms);
