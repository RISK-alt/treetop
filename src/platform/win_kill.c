#include "platform.h"

#include <windows.h>

/*
** This is the one function in the whole codebase whose entire job is to
** end a process, so it is deliberately paranoid at every step - see the
** brief: "the difference between killing what the user chose and killing
** a stranger."
**
** PID 0 (System Idle Process) and PID 4 (System) are refused before
** OpenProcess() is even attempted - src/input/keys.c already refuses to
** open a confirmation dialog naming either, but that refusal must never
** be the ONLY one: this is the final gate a caller cannot route around,
** not even accidentally.
**
** The core safety property - re-resolve by (pid, create_time) immediately
** before terminating - is what OpenProcess() + GetProcessTimes() +
** the QuadPart comparison below do together: a handle opened by PID
** alone says nothing about WHICH process instance that PID currently
** names, since Windows recycles PIDs within seconds of a process
** exiting. Comparing the freshly-read creation time against the key the
** caller asked to kill, on the handle just opened, is what turns "kill
** whatever currently holds this PID" into "kill the exact process
** instance the user confirmed, or refuse."
**
** Return contract (platform.h): 0 success, -1 denied (the caller should
** suggest running elevated), -2 already gone (or, equivalently, this key
** no longer names anything - a stale key from a process that exited, or
** whose PID was recycled by something else entirely, is treated the
** same as "already gone" rather than "denied", since neither case is
** something re-running elevated would fix).
*/
int     plat_kill(t_proc_key key)
{
    HANDLE          h;
    FILETIME        creation;
    FILETIME        exit_time;
    FILETIME        kernel_ft;
    FILETIME        user_ft;
    ULARGE_INTEGER  u;
    DWORD           err;

    if (key.pid == 0 || key.pid == 4)
        return (-1);
    h = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE, key.pid);
    if (h == NULL)
    {
        /*
        ** ERROR_ACCESS_DENIED means the PID is real but off-limits (a
        ** protected process, or this process lacks the privilege) - a
        ** genuine denial. Every other failure here (most commonly
        ** ERROR_INVALID_PARAMETER, which is what OpenProcess reports for
        ** a PID nothing currently holds) means there is nothing left to
        ** deny access to: the process is simply gone.
        */
        err = GetLastError();
        if (err == ERROR_ACCESS_DENIED)
            return (-1);
        return (-2);
    }
    if (!GetProcessTimes(h, &creation, &exit_time, &kernel_ft, &user_ft))
    {
        /* Opened a handle but cannot even read its creation time: too
           little information to safely proceed, so this refuses rather
           than guessing. */
        CloseHandle(h);
        return (-1);
    }
    u.LowPart = creation.dwLowDateTime;
    u.HighPart = creation.dwHighDateTime;
    if (u.QuadPart != (unsigned long long)key.create_time)
    {
        /*
        ** The identity check. This PID exists right now, but the process
        ** that currently holds it was created at a different time than
        ** the one the caller confirmed - the original process exited and
        ** Windows handed this PID to someone new. Terminating it would
        ** kill a process the user never saw. Reported as -2 ("already
        ** gone"), because from the caller's point of view the process it
        ** actually asked to kill IS gone - what is left under this PID
        ** is a stranger.
        */
        CloseHandle(h);
        return (-2);
    }
    if (!TerminateProcess(h, 1))
    {
        CloseHandle(h);
        return (-1);
    }
    CloseHandle(h);
    return (0);
}
