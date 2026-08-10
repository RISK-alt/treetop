#include "ntapi.h"

#include <string.h>

/*
** The two ntdll entry points, resolved once with GetProcAddress and
** cached here. Never linked against ntdll.lib - that would pull the
** DDK onto the list of build requirements for what is otherwise a
** plain Win32 project.
*/
typedef LONG (NTAPI *t_fn_query_system)(ULONG, PVOID, ULONG, PULONG);
typedef LONG (NTAPI *t_fn_query_process)(HANDLE, ULONG, PVOID, ULONG, PULONG);

static t_fn_query_system   g_query_system;
static t_fn_query_process  g_query_process;
static int                 g_initialized;
static int                 g_available;

/*
** GetProcAddress returns FARPROC; casting that straight to a differently
** shaped function pointer trips -Wcast-function-type. Copying the bytes
** through memcpy says the same thing without the warning, and is safe
** here because FARPROC and our typedefs are both ordinary code pointers
** on this platform.
*/
static void resolve(HMODULE mod, const char *name, void *out_fn)
{
    FARPROC proc;

    proc = GetProcAddress(mod, name);
    memcpy(out_fn, &proc, sizeof(proc));
}

int     ntapi_init(void)
{
    HMODULE ntdll;

    if (g_initialized)
        return (g_available ? 0 : 1);
    g_initialized = 1;
    ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == NULL)
        return (1);
    resolve(ntdll, "NtQuerySystemInformation", &g_query_system);
    resolve(ntdll, "NtQueryInformationProcess", &g_query_process);
    g_available = (g_query_system != NULL && g_query_process != NULL);
    return (g_available ? 0 : 1);
}

int     ntapi_available(void)
{
    return (g_available);
}

LONG    nt_query_system(ULONG cls, PVOID buf, ULONG len, PULONG ret)
{
    if (g_query_system == NULL)
        return (-1);
    return (g_query_system(cls, buf, len, ret));
}

LONG    nt_query_process(HANDLE h, ULONG cls, PVOID buf, ULONG len,
            PULONG ret)
{
    if (g_query_process == NULL)
        return (-1);
    return (g_query_process(h, cls, buf, len, ret));
}
