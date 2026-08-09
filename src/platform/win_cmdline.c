#include "platform.h"
#include "ntapi.h"

#include <windows.h>
#include <stdlib.h>
#include <string.h>

/*
** This is the feature the whole tool exists for: without it, every
** node.exe or python.exe row in the tree is indistinguishable from every
** other one. See platform.h for the cache's lifetime contract - callers
** rely on it, so it is documented there, not repeated here.
*/

/*                                 CONSTANTS                                  */

/*
** Neither path should ever legitimately need more than this. The real
** Windows command-line limit is 32767 wide characters (~64 KiB); this
** cap is double that plus header slack, so it can only ever reject a
** hostile or corrupted length, never a real one. Bounds the allocation
** in the primary path and the ReadProcessMemory in the PEB fallback.
*/
#define TT_CMDLINE_MAX_BYTES  (128 * 1024)

/*                                  CACHE                                     */

typedef struct s_cmd_entry
{
    t_proc_key  key;
    wchar_t     *value;
}   t_cmd_entry;

static t_cmd_entry     *g_cache;
static size_t           g_count;
static size_t           g_capacity;

static t_cmd_entry *cache_find(t_proc_key key)
{
    size_t  i;

    i = 0;
    while (i < g_count)
    {
        if (key_eq(g_cache[i].key, key))
            return (&g_cache[i]);
        i++;
    }
    return (NULL);
}

/*
** Grows the backing array by doubling before appending. On realloc
** failure the original array - and every pointer already stored in it
** - is untouched and still owned by g_cache; only the new entry is
** dropped, so a transient allocation failure costs one retry next tick
** rather than leaking or losing what is already cached.
*/
static int  cache_add(t_proc_key key, wchar_t *value)
{
    t_cmd_entry *grown;
    size_t      cap;

    if (g_count == g_capacity)
    {
        if (g_capacity == 0)
            cap = 64;
        else
            cap = g_capacity * 2;
        grown = realloc(g_cache, cap * sizeof(t_cmd_entry));
        if (grown == NULL)
            return (-1);
        g_cache = grown;
        g_capacity = cap;
    }
    g_cache[g_count].key = key;
    g_cache[g_count].value = value;
    g_count++;
    return (0);
}

/*                          PRIMARY PATH: NT (CLASS 60)                       */

/*
** buf holds a UNICODE_STRING immediately followed by its character data
** in the same allocation, per ProcessCommandLineInformation's contract.
** Buffer points at that trailing data, Length is a byte count, and the
** data is not NUL-terminated - the same three traps win_process.c's
** image_from_unicode() documents for ImageName, plus one more: the
** pointer inside a NtQueryInformationProcess result is untrusted enough
** here (a corrupted or hostile answer would be a garbage command line,
** not a crash) that its range is checked against the buffer it actually
** came from before anything is copied out of it.
*/
static wchar_t  *copy_cmdline_result(const unsigned char *buf, ULONG got)
{
    const UNICODE_STRING   *us;
    const unsigned char     *data_start;
    const unsigned char     *data_end;
    size_t                   chars;
    wchar_t                 *out;

    if (got < sizeof(UNICODE_STRING))
        return (NULL);
    us = (const UNICODE_STRING *)buf;
    if (us->Buffer == NULL || us->Length == 0)
        return (NULL);
    data_start = (const unsigned char *)us->Buffer;
    data_end = data_start + us->Length;
    if (data_start < buf + sizeof(UNICODE_STRING) || data_end > buf + got)
        return (NULL);
    chars = us->Length / sizeof(wchar_t);
    out = malloc((chars + 1) * sizeof(wchar_t));
    if (out == NULL)
        return (NULL);
    memcpy(out, us->Buffer, chars * sizeof(wchar_t));
    out[chars] = L'\0';
    return (out);
}

/*
** Two-call NtQueryInformationProcess pattern: a zero-length probe to
** learn the size, then one real call. The probe's own status is not
** worth inspecting - every refusal this file cares about (access
** denied, class unsupported, ntdll unresolved) leaves `needed` at the
** zero it was initialised to, so a single "did we get a plausible size"
** check covers all of them without needing to know which status code a
** given failure mode returns.
*/
static wchar_t  *fetch_primary(HANDLE h)
{
    ULONG           needed;
    ULONG           got;
    LONG            status;
    unsigned char   *buf;
    wchar_t         *out;

    needed = 0;
    nt_query_process(h, ProcessCommandLineInformation, NULL, 0, &needed);
    if (needed == 0 || needed > TT_CMDLINE_MAX_BYTES)
        return (NULL);
    buf = malloc(needed);
    if (buf == NULL)
        return (NULL);
    got = 0;
    status = nt_query_process(h, ProcessCommandLineInformation, buf, needed,
            &got);
    if (status != 0)
    {
        free(buf);
        return (NULL);
    }
    if (got > needed)
        got = needed;
    out = copy_cmdline_result(buf, got);
    free(buf);
    return (out);
}

/*                          FALLBACK PATH: PEB READ                           */

/*
** Reads PEB -> ProcessParameters -> CommandLine directly out of the
** target's address space. Every ReadProcessMemory call below is checked
** for both a nonzero return AND the exact byte count expected - a short
** read is treated as a total failure, never as a string to truncate and
** show anyway, since a partial UNICODE_STRING or partial buffer pointer
** is not a shorter command line, it is garbage.
**
** IsWow64Process gates this entirely: the offsets above are the x64
** layout only, and treetop never runs the 32-bit layout it would need
** for a WOW64 target. Reading through the wrong offsets would not fail
** cleanly - it would produce a plausible-looking wrong answer - so a
** WOW64 target is refused before either offset is ever used.
*/
static wchar_t  *fetch_fallback_peb(unsigned long pid)
{
    HANDLE                          h;
    BOOL                             is_wow64;
    TT_PROCESS_BASIC_INFORMATION     pbi;
    ULONG                            ret;
    LONG                             status;
    PVOID                            params_ptr;
    UNICODE_STRING                   cmd_us;
    SIZE_T                           done;
    size_t                           chars;
    wchar_t                          *out;

    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
            FALSE, pid);
    if (h == NULL)
        return (NULL);
    is_wow64 = FALSE;
    if (!IsWow64Process(h, &is_wow64) || is_wow64)
    {
        CloseHandle(h);
        return (NULL);
    }
    memset(&pbi, 0, sizeof(pbi));
    ret = 0;
    status = nt_query_process(h, ProcessBasicInformation, &pbi, sizeof(pbi),
            &ret);
    if (status != 0 || pbi.PebBaseAddress == NULL)
    {
        CloseHandle(h);
        return (NULL);
    }
    params_ptr = NULL;
    done = 0;
    if (!ReadProcessMemory(h, (unsigned char *)pbi.PebBaseAddress
                + TT_PEB_PROCESS_PARAMETERS_OFFSET,
            &params_ptr, sizeof(params_ptr), &done)
        || done != sizeof(params_ptr) || params_ptr == NULL)
    {
        CloseHandle(h);
        return (NULL);
    }
    memset(&cmd_us, 0, sizeof(cmd_us));
    done = 0;
    if (!ReadProcessMemory(h, (unsigned char *)params_ptr
                + TT_RTL_USER_PROCESS_PARAMS_CMDLINE_OFFSET,
            &cmd_us, sizeof(cmd_us), &done)
        || done != sizeof(cmd_us))
    {
        CloseHandle(h);
        return (NULL);
    }
    /*
    ** No TT_CMDLINE_MAX_BYTES check needed here: Length is a USHORT, so
    ** it is already hard-capped at 65535 bytes by its type - unlike the
    ** primary path's `needed`, which is a ULONG ntdll fills in and so
    ** has no such built-in ceiling.
    */
    if (cmd_us.Buffer == NULL || cmd_us.Length == 0)
    {
        CloseHandle(h);
        return (NULL);
    }
    chars = cmd_us.Length / sizeof(wchar_t);
    out = malloc((chars + 1) * sizeof(wchar_t));
    if (out == NULL)
    {
        CloseHandle(h);
        return (NULL);
    }
    done = 0;
    if (!ReadProcessMemory(h, cmd_us.Buffer, out, cmd_us.Length, &done)
        || done != (SIZE_T)cmd_us.Length)
    {
        free(out);
        CloseHandle(h);
        return (NULL);
    }
    out[chars] = L'\0';
    CloseHandle(h);
    return (out);
}

/*                                   FETCH                                    */

/*
** Tries the light-weight NT class first and only opens a second, more
** privileged handle for the PEB fallback when that fails - a protected
** process refuses both identically, but most processes succeed on the
** first handle alone. Every handle opened here is closed on every path
** before returning, including when OpenProcess itself is what failed.
*/
static wchar_t  *fetch_cmdline(unsigned long pid)
{
    HANDLE      h;
    wchar_t     *out;

    out = NULL;
    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h != NULL)
    {
        out = fetch_primary(h);
        CloseHandle(h);
    }
    if (out == NULL)
        out = fetch_fallback_peb(pid);
    return (out);
}

/*                                ENTRY POINTS                                */

/*
** A cache hit returns the stored pointer even when that pointer is
** NULL: a process that refused once will refuse identically on every
** later call, and retrying a few hundred handles a second to relearn
** nothing would be pure waste. Only a genuine miss (the key has never
** been looked up before) reaches fetch_cmdline().
*/
const wchar_t   *plat_cmdline(t_proc_key key)
{
    t_cmd_entry     *entry;
    wchar_t         *value;

    entry = cache_find(key);
    if (entry != NULL)
        return (entry->value);
    value = fetch_cmdline(key.pid);
    if (cache_add(key, value) != 0)
    {
        free(value);
        return (NULL);
    }
    return (value);
}

/*
** Evicts every cached entry whose key is absent from `tbl`. Safe under
** the contract documented on plat_cmdline() in platform.h: a
** (pid, create_time) key identifies one process instance forever, so
** "absent from a freshly sampled table" means that instance has exited
** and no future lookup for that exact key can occur.
**
** O(cache_size * tbl->count): at treetop's real scale - a few hundred
** processes - that is at most a few hundred thousand key comparisons,
** immeasurable next to the OpenProcess/NtQueryInformationProcess calls
** this cache exists to avoid. It would not scale to a machine with tens
** of thousands of processes, but no real Windows system carries that
** many; a hash-keyed cache was considered and rejected as complexity
** this scale does not need.
*/
void    plat_cmdline_sweep(const t_table *tbl)
{
    size_t  i;
    size_t  j;
    int     alive;

    i = 0;
    while (i < g_count)
    {
        alive = 0;
        j = 0;
        while (j < tbl->count)
        {
            if (key_eq(tbl->procs[j].key, g_cache[i].key))
            {
                alive = 1;
                break ;
            }
            j++;
        }
        if (alive)
            i++;
        else
        {
            free(g_cache[i].value);
            g_cache[i] = g_cache[g_count - 1];
            g_count--;
        }
    }
}

/*
** Frees every cached string and the backing array itself. Declared in
** platform.h and, until this task, stubbed out in win_system.c with a
** comment noting nothing yet needed freeing - this cache is the first
** thing in the codebase that does.
*/
void    plat_shutdown(void)
{
    size_t  i;

    i = 0;
    while (i < g_count)
    {
        free(g_cache[i].value);
        i++;
    }
    free(g_cache);
    g_cache = NULL;
    g_count = 0;
    g_capacity = 0;
}
