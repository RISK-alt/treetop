#include "platform.h"
#include "ntapi.h"

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdlib.h>
#include <string.h>

/*
** tlhelp32.h and psapi.h are only used by the ToolHelp32 fallback below;
** the primary path talks to ntdll exclusively through ntapi.h.
*/

/*                                 CONSTANTS                                  */

/*
** NtQuerySystemInformation's own status code for "buffer too small".
** Pulling this from <ntstatus.h> would drag in a header that redefines
** a handful of NTSTATUS codes windows.h already declares under
** different guards; a local constant with the one value this file
** checks avoids that collision entirely.
*/
#define TT_STATUS_INFO_LENGTH_MISMATCH  ((LONG)0xC0000004)

/*
** Growth starts at 256 KB (comfortably above a quiet system's snapshot)
** and doubles on each STATUS_INFO_LENGTH_MISMATCH. Eight doublings caps
** the buffer at 64 MB - far past any real process table - so a system
** whose process count keeps changing faster than we can catch up gives
** up after a bounded number of tries instead of growing without limit.
*/
#define TT_QUERY_INITIAL_BYTES          (256 * 1024)
#define TT_QUERY_MAX_RETRIES            8

/*
** A second, independent bound on the entry walk below: even with a
** buffer-size check on every step, a hard cap on iteration count means
** a malformed or truncated NextEntryOffset chain can never turn into an
** unbounded loop. Real systems today carry a few thousand processes at
** most; this is generously above that.
*/
#define TT_WALK_MAX_ENTRIES             100000

/*                                IMAGE NAME                                  */

/*
** ImageName has three traps: Buffer is NULL for PID 0 (the idle
** process has no image), Length is a byte count rather than a
** wchar_t count, and the bytes it does point at are not
** NUL-terminated. wcsncpy does not NUL-terminate when the source is at
** least as long as the destination, so termination is written
** explicitly rather than trusted to it.
*/
static void image_from_unicode(const UNICODE_STRING *src, wchar_t *out)
{
    size_t  chars;

    if (src->Buffer == NULL)
    {
        wcsncpy(out, L"System Idle Process", TT_IMAGE_LEN - 1);
        out[TT_IMAGE_LEN - 1] = L'\0';
        return ;
    }
    chars = src->Length / sizeof(wchar_t);
    if (chars > (size_t)TT_IMAGE_LEN - 1)
        chars = (size_t)TT_IMAGE_LEN - 1;
    memcpy(out, src->Buffer, chars * sizeof(wchar_t));
    out[chars] = L'\0';
}

/*                              PRIMARY PATH: NT                              */

/*
** Queries TT_SystemProcessInformation into a heap buffer that grows to fit,
** bounded by TT_QUERY_MAX_RETRIES. On success *out_buf owns the buffer
** (caller frees it) and *out_len is the number of bytes NtQuerySystem-
** Information actually wrote - the bound the entry walk trusts instead
** of trusting NextEntryOffset chains alone.
*/
static int query_process_snapshot(unsigned char **out_buf, ULONG *out_len)
{
    unsigned char   *buf;
    unsigned char   *grown;
    ULONG           cap;
    ULONG           got;
    LONG            status;
    int             tries;

    cap = TT_QUERY_INITIAL_BYTES;
    buf = malloc(cap);
    if (buf == NULL)
        return (-1);
    tries = 0;
    while (tries < TT_QUERY_MAX_RETRIES)
    {
        got = 0;
        status = nt_query_system(TT_SystemProcessInformation, buf, cap,
                &got);
        if (status == 0)
        {
            *out_buf = buf;
            *out_len = got;
            return (0);
        }
        if (status != TT_STATUS_INFO_LENGTH_MISMATCH)
            break ;
        cap *= 2;
        grown = realloc(buf, cap);
        if (grown == NULL)
            break ;
        buf = grown;
        tries++;
    }
    free(buf);
    return (-1);
}

/*
** Reserved[0] is documented in ntapi.h as WorkingSetPrivateSize since
** Vista - see the comment on TT_SYSTEM_PROCESS_INFORMATION for why that
** field stays folded into Reserved[3] rather than being split out by
** name.
*/
static void fill_from_nt_entry(t_process *p,
        const TT_SYSTEM_PROCESS_INFORMATION *e)
{
    memset(p, 0, sizeof(*p));
    p->key.pid = (unsigned long)(ULONG_PTR)e->UniqueProcessId;
    p->key.create_time = (unsigned long long)e->CreateTime.QuadPart;
    p->ppid = (unsigned long)(ULONG_PTR)e->InheritedFromUniqueProcessId;
    p->thread_count = e->NumberOfThreads;
    p->kernel_time = (unsigned long long)e->KernelTime.QuadPart;
    p->user_time = (unsigned long long)e->UserTime.QuadPart;
    if (e->Reserved[0].QuadPart != 0)
        p->working_set = (unsigned long long)e->Reserved[0].QuadPart;
    else
        p->working_set =
            (unsigned long long)e->VirtualMemoryCounters.WorkingSetSize;
    image_from_unicode(&e->ImageName, p->image);
}

/*
** Walks NextEntryOffset chains inside a buffer of `len` bytes, adding
** one table row per entry. Two independent bounds keep a malformed or
** truncated buffer from being walked past its end: the loop condition
** never dereferences an entry whose header would cross `len`, and the
** offset arithmetic is done in 64 bits and checked to strictly advance
** before it is narrowed back to the ULONG the struct actually uses, so
** a garbage NextEntryOffset can neither read out of bounds nor spin the
** walk forever by wrapping back on itself.
*/
static int enumerate_nt(t_table *tbl)
{
    unsigned char                          *buf;
    ULONG                                   len;
    ULONG                                   offset;
    unsigned long long                      next;
    const TT_SYSTEM_PROCESS_INFORMATION    *entry;
    t_process                               proc;
    size_t                                  before;
    int                                     walked;

    if (query_process_snapshot(&buf, &len) != 0)
        return (-1);
    before = tbl->count;
    offset = 0;
    walked = 0;
    while (offset + sizeof(TT_SYSTEM_PROCESS_INFORMATION) <= len
           && walked < TT_WALK_MAX_ENTRIES)
    {
        entry = (const TT_SYSTEM_PROCESS_INFORMATION *)(buf + offset);
        fill_from_nt_entry(&proc, entry);
        if (table_add(tbl, &proc) == NULL)
            break ;
        if (entry->NextEntryOffset == 0)
            break ;
        next = (unsigned long long)offset + entry->NextEntryOffset;
        if (next <= offset || next > len)
            break ;
        offset = (ULONG)next;
        walked++;
    }
    free(buf);
    if (tbl->count == before)
        return (-1);
    return (0);
}

/*                          FALLBACK PATH: TOOLHELP32                         */

/*
** OpenProcess can fail here (protected processes, processes that exit
** between the snapshot and this call, access denied under a restricted
** token). When it does, create_time is deliberately left at 0 rather
** than guessed. Every key comparison elsewhere in this codebase treats
** a process as identified by (pid, create_time) together, so a 0
** create_time can never match a real parent's create_time when this
** process is looked up later as somebody's parent - its children fall
** back to being roots instead of being mis-attached to an unrelated
** process that happens to reuse the pid, and it is never flagged as an
** orphan either. Degrading to "no claim" beats a wrong claim.
*/
static void fill_from_snapshot(t_process *p, const PROCESSENTRY32W *pe)
{
    HANDLE                    h;
    FILETIME                  creation;
    FILETIME                  exit_time;
    FILETIME                  kernel_ft;
    FILETIME                  user_ft;
    PROCESS_MEMORY_COUNTERS   pmc;
    ULARGE_INTEGER            u;

    memset(p, 0, sizeof(*p));
    p->key.pid = pe->th32ProcessID;
    p->ppid = pe->th32ParentProcessID;
    p->thread_count = pe->cntThreads;
    wcsncpy(p->image, pe->szExeFile, TT_IMAGE_LEN - 1);
    p->image[TT_IMAGE_LEN - 1] = L'\0';
    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
            pe->th32ProcessID);
    if (h == NULL)
        return ;
    if (GetProcessTimes(h, &creation, &exit_time, &kernel_ft, &user_ft))
    {
        u.LowPart = creation.dwLowDateTime;
        u.HighPart = creation.dwHighDateTime;
        p->key.create_time = u.QuadPart;
        u.LowPart = kernel_ft.dwLowDateTime;
        u.HighPart = kernel_ft.dwHighDateTime;
        p->kernel_time = u.QuadPart;
        u.LowPart = user_ft.dwLowDateTime;
        u.HighPart = user_ft.dwHighDateTime;
        p->user_time = u.QuadPart;
    }
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc)))
        p->working_set = pmc.WorkingSetSize;
    CloseHandle(h);
}

static int enumerate_toolhelp(t_table *tbl)
{
    HANDLE            snap;
    PROCESSENTRY32W   pe;
    t_process         proc;
    size_t            before;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return (-1);
    before = tbl->count;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe))
    {
        do
        {
            fill_from_snapshot(&proc, &pe);
            if (table_add(tbl, &proc) == NULL)
                break ;
        }
        while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    if (tbl->count == before)
        return (-1);
    return (0);
}

/*                                ENTRY POINT                                 */

/*
** Clears the table and refills it via whichever path ntapi_init()
** decided is available. Sets key, ppid, image, kernel_time, user_time,
** working_set and thread_count on every row; cmdline, ports and every
** derived field are left at zero/NULL for later tasks to fill in.
**
** Returns 0 when at least one process was added, -1 only when neither
** path could add anything at all - a table with some processes from a
** partial failure is still more useful to the caller than an empty one.
*/
int     plat_processes(t_table *tbl)
{
    table_clear(tbl);
    if (ntapi_available())
        return (enumerate_nt(tbl));
    return (enumerate_toolhelp(tbl));
}
