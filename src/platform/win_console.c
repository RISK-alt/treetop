#include "console.h"

#include <windows.h>
#include <wchar.h>
#include <stdlib.h>

/*
** This file owns real, global console state that outlives the process
** view of it - two saved DWORD modes and an alternate screen buffer -
** so every function here is written around one question: no matter how
** the process leaves (q, Ctrl+C, a crash that still reaches atexit),
** does the terminal come back the way it was found?
*/

/*                                 CONSTANTS                                  */

/*
** WriteConsoleW has a long-documented history of choking on very large
** single calls (older conhost builds cap out well under 64 KiB per
** call, sometimes silently writing less than asked). No frame treetop
** renders comes close to this, but con_write() chunks defensively
** anyway rather than betting correctness on today's console host
** behaving the same way forever.
*/
# define TT_CON_CHUNK       8192u

# define TT_SEQ_ALT_ON      L"\x1b[?1049h"
# define TT_SEQ_ALT_OFF     L"\x1b[?1049l"
# define TT_SEQ_CURSOR_ON   L"\x1b[?25h"
# define TT_SEQ_CURSOR_OFF  L"\x1b[?25l"

/*                                   STATE                                    */

static HANDLE           g_hout;
static HANDLE           g_hin;
static DWORD             g_orig_out_mode;
static DWORD             g_orig_in_mode;
static int               g_active;
static volatile LONG     g_shutdown_done;

/*                                  OUTPUT                                    */

/*
** A single call is not guaranteed to consume everything it is given -
** WriteConsoleW can both refuse buffers above some internal limit and
** report a partial `written` count on a call that otherwise succeeds -
** so this loops on both the chunk boundary and on whatever `written`
** actually reports, rather than trusting either one to match `len`.
*/
void    con_write(const wchar_t *buf, size_t len)
{
    size_t  remaining;
    size_t  chunk;
    DWORD   written;

    if (g_hout == NULL || g_hout == INVALID_HANDLE_VALUE || buf == NULL)
        return ;
    remaining = len;
    while (remaining > 0)
    {
        chunk = remaining;
        if (chunk > TT_CON_CHUNK)
            chunk = TT_CON_CHUNK;
        written = 0;
        if (!WriteConsoleW(g_hout, buf, (DWORD)chunk, &written, NULL))
            return ;
        if (written == 0)
            return ;
        buf += written;
        remaining -= written;
    }
}

/*                                   SIZE                                     */

void    con_size(int *cols, int *rows)
{
    CONSOLE_SCREEN_BUFFER_INFO     info;

    if (cols != NULL)
        *cols = 0;
    if (rows != NULL)
        *rows = 0;
    if (g_hout == NULL || g_hout == INVALID_HANDLE_VALUE)
        return ;
    if (!GetConsoleScreenBufferInfo(g_hout, &info))
        return ;
    if (cols != NULL)
        *cols = (int)(info.srWindow.Right - info.srWindow.Left + 1);
    if (rows != NULL)
        *rows = (int)(info.srWindow.Bottom - info.srWindow.Top + 1);
}

/*                                LIFECYCLE                                   */

static BOOL WINAPI ctrl_handler(DWORD ctrl_type)
{
    (void)ctrl_type;
    con_shutdown();
    /*
    ** FALSE hands the event on to the next handler and ultimately the
    ** default one, which terminates the process. Returning TRUE would
    ** claim the event as fully handled and leave the process running -
    ** not what a user hitting Ctrl+C on a monitor wants. Restoration
    ** above already ran synchronously on this thread, so by the time
    ** the default handler tears the process down the console is clean.
    */
    return (FALSE);
}

/*
** Every step below either changes nothing on failure or undoes exactly
** what it just changed before returning -1: con_init never leaves the
** console half-modified. Both GetConsoleMode calls happen before
** anything is written, so g_orig_out_mode/g_orig_in_mode always hold
** the real pre-treetop state by the time either SetConsoleMode call
** runs, and a failure of the second SetConsoleMode rolls the first one
** back rather than returning with the output mode still changed.
*/
int     con_init(void)
{
    DWORD   out_mode;
    DWORD   in_mode;

    g_hout = GetStdHandle(STD_OUTPUT_HANDLE);
    g_hin = GetStdHandle(STD_INPUT_HANDLE);
    if (g_hout == NULL || g_hout == INVALID_HANDLE_VALUE
        || g_hin == NULL || g_hin == INVALID_HANDLE_VALUE)
        return (-1);
    if (!GetConsoleMode(g_hout, &g_orig_out_mode)
        || !GetConsoleMode(g_hin, &g_orig_in_mode))
        return (-1);
    out_mode = g_orig_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING
            | ENABLE_PROCESSED_OUTPUT;
    if (!SetConsoleMode(g_hout, out_mode))
        return (-1);
    /*
    ** Only ENABLE_VIRTUAL_TERMINAL_PROCESSING failing is fatal: that is
    ** the one flag whose absence turns every escape sequence this file
    ** writes into literal garbage on screen. The output code page does
    ** not affect WriteConsoleW at all - it takes UTF-16 directly - so a
    ** failure here is purely cosmetic (it would only matter to
    ** narrow-char output this file never does) and not worth tearing
    ** init down for.
    */
    SetConsoleOutputCP(CP_UTF8);
    /*
    ** ENABLE_QUICK_EDIT_MODE is only honoured when ENABLE_EXTENDED_FLAGS
    ** is set in the *same* SetConsoleMode call; omit it and Windows
    ** silently keeps quick-edit on, so a stray click still selects text
    ** and freezes the display. Leaving ENABLE_LINE_INPUT and
    ** ENABLE_ECHO_INPUT out of the mode entirely (rather than clearing
    ** bits that would otherwise be set) is what turns every keystroke
    ** into a raw KEY_EVENT instead of a buffered, echoed line.
    */
    in_mode = ENABLE_WINDOW_INPUT | ENABLE_PROCESSED_INPUT
            | ENABLE_EXTENDED_FLAGS;
    if (!SetConsoleMode(g_hin, in_mode))
    {
        SetConsoleMode(g_hout, g_orig_out_mode);
        return (-1);
    }
    g_active = 1;
    SetConsoleCtrlHandler(ctrl_handler, TRUE);
    atexit(con_shutdown);
    con_write(TT_SEQ_ALT_ON, wcslen(TT_SEQ_ALT_ON));
    con_write(TT_SEQ_CURSOR_OFF, wcslen(TT_SEQ_CURSOR_OFF));
    return (0);
}

/*
** Guarded by an interlocked flag rather than a plain int: this can run
** from the main thread via atexit and, concurrently, from the separate
** thread Windows spawns to run the console control handler on Ctrl+C.
** That second thread is an ordinary OS thread, not a POSIX signal
** context - every Win32 call made here (WriteConsoleW, SetConsoleMode)
** is as safe from it as from anywhere else - but two threads racing to
** run this body once each is still a real hazard, and the interlocked
** compare-and-swap is what closes it off. A main-thread con_write()
** that is mid-flight when Ctrl+C lands may interleave with the exit
** sequence below; that is harmless (the process is exiting either way)
** and the SetConsoleMode calls that actually restore the terminal
** still run to completion regardless.
*/
void    con_shutdown(void)
{
    if (InterlockedCompareExchange(&g_shutdown_done, 1, 0) != 0)
        return ;
    if (!g_active)
        return ;
    con_write(TT_SEQ_CURSOR_ON, wcslen(TT_SEQ_CURSOR_ON));
    con_write(TT_SEQ_ALT_OFF, wcslen(TT_SEQ_ALT_OFF));
    SetConsoleMode(g_hout, g_orig_out_mode);
    SetConsoleMode(g_hin, g_orig_in_mode);
}

/*                                   INPUT                                    */

static int  map_key(const KEY_EVENT_RECORD *ev)
{
    WORD    vk;

    vk = ev->wVirtualKeyCode;
    if (vk == VK_F9 && (ev->dwControlKeyState & SHIFT_PRESSED) != 0)
        return (TT_KEY_SHIFT_F9);
    if (vk == VK_UP)
        return (TT_KEY_UP);
    if (vk == VK_DOWN)
        return (TT_KEY_DOWN);
    if (vk == VK_LEFT)
        return (TT_KEY_LEFT);
    if (vk == VK_RIGHT)
        return (TT_KEY_RIGHT);
    if (vk == VK_F1)
        return (TT_KEY_F1);
    if (vk == VK_F5)
        return (TT_KEY_F5);
    if (vk == VK_F6)
        return (TT_KEY_F6);
    if (vk == VK_F9)
        return (TT_KEY_F9);
    if (vk == VK_ESCAPE)
        return (TT_KEY_ESCAPE);
    if (vk == VK_RETURN)
        return (TT_KEY_ENTER);
    if (vk == VK_BACK)
        return (TT_KEY_BACKSPACE);
    if (ev->uChar.UnicodeChar != 0)
        return ((int)ev->uChar.UnicodeChar);
    return (0);
}

/*
** WaitForSingleObject only proves the input queue is non-empty, not
** that the next record is a keystroke worth reporting: focus changes,
** the menu-key event Alt alone generates, and a bare modifier key-down
** (Shift/Ctrl/Alt pressed with nothing else yet) all arrive as real
** events, and map_key() returns 0 for anything it does not recognise.
** Returning that 0 to the caller would make it indistinguishable from
** a genuine timeout, so instead the loop consumes the event and waits
** again against the *remaining* budget - 0 is only ever returned when
** the deadline itself is reached, never as a side effect of an event
** this function chose to ignore.
*/
int     con_wait_key(unsigned int timeout_ms)
{
    unsigned long long     deadline;
    unsigned long long     now;
    DWORD                   wait_ms;
    INPUT_RECORD            rec;
    DWORD                   got;
    int                     key;

    if (g_hin == NULL || g_hin == INVALID_HANDLE_VALUE)
        return (0);
    deadline = GetTickCount64() + timeout_ms;
    while (1)
    {
        now = GetTickCount64();
        if (now >= deadline)
            return (0);
        wait_ms = (DWORD)(deadline - now);
        if (WaitForSingleObject(g_hin, wait_ms) != WAIT_OBJECT_0)
            return (0);
        got = 0;
        if (!ReadConsoleInputW(g_hin, &rec, 1, &got) || got == 0)
            return (0);
        if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT)
            return (TT_KEY_RESIZE);
        if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown)
            continue ;
        key = map_key(&rec.Event.KeyEvent);
        if (key != 0)
            return (key);
    }
}
