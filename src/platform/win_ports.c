/*
** winsock2.h must be included before windows.h: windows.h pulls in the
** old winsock 1.1 declarations unless winsock2.h has already defined the
** guard that keeps it out, and ntohs() only exists in winsock2.h. ntapi.h
** below includes windows.h itself, so this ordering is what protects it.
*/
#include <winsock2.h>

#include "platform.h"
#include "ntapi.h"

#include <iphlpapi.h>
#include <stddef.h>
#include <stdlib.h>

/*                                 CONSTANTS                                  */

/*
** GetExtendedTcpTable's sizing call and its real call are not atomic:
** the table can grow between them if a process starts listening in that
** window. Looping on ERROR_INSUFFICIENT_BUFFER handles that, bounded so
** a table that keeps growing faster than this can allocate gives up
** instead of retrying forever. Four tries is generous - it would take
** four consecutive listen()s landing in that exact window to exhaust it.
*/
#define TT_PORTS_MAX_RETRIES  4

/*                                  CLEAR                                     */

static void clear_all_ports(t_table *tbl)
{
    size_t  i;

    i = 0;
    while (i < tbl->count)
    {
        tbl->procs[i].port_count = 0;
        i++;
    }
}

/*                                 ATTRIBUTE                                  */

/*
** table_find_pid, not table_find: the TCP owner-PID table carries only a
** PID, no creation time, so the (pid, create_time) identity the rest of
** this codebase relies on is not available here. This is the one
** deliberate exception to that rule. The exposure is bounded to a single
** refresh interval - a port can be misattributed to a just-started
** process that reused a just-exited one's PID until the next sample - and
** that bounded, briefly-wrong attribution is a better trade than
** inventing a creation time to match on, which would just be a different
** kind of wrong dressed up as precise.
*/
static void attribute_port(t_table *tbl, unsigned long pid,
        unsigned short port)
{
    t_process   *p;
    int         i;

    p = table_find_pid(tbl, pid);
    if (p == NULL)
        return ;
    i = 0;
    while (i < p->port_count)
    {
        if (p->ports[i] == port)
            return ;
        i++;
    }
    if (p->port_count < TT_MAX_PORTS)
        p->ports[p->port_count++] = port;
}

/*                                   FETCH                                    */

/*
** Two-call sizing pattern shared by both address families: size, alloc,
** call again, looping on ERROR_INSUFFICIENT_BUFFER up to
** TT_PORTS_MAX_RETRIES times since the table can grow between the sizing
** call and the real one. On any other failure, or on exhausting the
** retry bound, the buffer is freed and NULL is returned - callers treat
** that as "nothing to walk", not fatal to the whole function.
*/
static void *fetch_tcp_table(ULONG family, ULONG *out_size)
{
    void    *buf;
    ULONG   size;
    DWORD   err;
    int     tries;

    buf = NULL;
    size = 0;
    err = GetExtendedTcpTable(NULL, &size, FALSE, family,
            TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (err != ERROR_INSUFFICIENT_BUFFER || size == 0)
        return (NULL);
    tries = 0;
    while (tries < TT_PORTS_MAX_RETRIES)
    {
        free(buf);
        buf = malloc(size);
        if (buf == NULL)
            return (NULL);
        err = GetExtendedTcpTable(buf, &size, FALSE, family,
                TCP_TABLE_OWNER_PID_LISTENER, 0);
        if (err == NO_ERROR)
        {
            *out_size = size;
            return (buf);
        }
        if (err != ERROR_INSUFFICIENT_BUFFER)
            break ;
        tries++;
    }
    free(buf);
    return (NULL);
}

/*
** dwNumEntries comes from the same call that filled the buffer, but the
** walk still bounds itself independently against `size` (the byte count
** actually allocated and returned) rather than trusting dwNumEntries
** alone - the same defensive posture win_process.c takes with
** NextEntryOffset chains, applied here to a row count instead.
*/
static void walk_tcp4(t_table *tbl, const MIB_TCPTABLE_OWNER_PID *table,
        ULONG size)
{
    DWORD           i;
    DWORD           n;
    unsigned short  port;

    if (size < sizeof(MIB_TCPTABLE_OWNER_PID))
        return ;
    /*
    ** offsetof and sizeof are size_t (8 bytes on x64); the subtraction
    ** and division below happen in size_t before landing in the 4-byte
    ** DWORD n. Left implicit, MSVC's /W4 flags that narrowing as C4267
    ** and /WX turns it into a hard error. win_process.c's enumerate_nt()
    ** hit the same shape with NextEntryOffset arithmetic and narrows
    ** explicitly for the same reason; size is itself a ULONG and a TCP
    ** row is never zero bytes, so the quotient can never exceed what a
    ** DWORD holds - the cast documents that bound instead of leaving the
    ** compiler to guess it.
    */
    n = (DWORD)((size - offsetof(MIB_TCPTABLE_OWNER_PID, table))
        / sizeof(MIB_TCPROW_OWNER_PID));
    if (table->dwNumEntries < n)
        n = table->dwNumEntries;
    i = 0;
    while (i < n)
    {
        port = ntohs((u_short)table->table[i].dwLocalPort);
        attribute_port(tbl, table->table[i].dwOwningPid, port);
        i++;
    }
}

static void walk_tcp6(t_table *tbl, const MIB_TCP6TABLE_OWNER_PID *table,
        ULONG size)
{
    DWORD           i;
    DWORD           n;
    unsigned short  port;

    if (size < sizeof(MIB_TCP6TABLE_OWNER_PID))
        return ;
    /* Same MSVC C4267 narrowing risk as walk_tcp4() above; see there. */
    n = (DWORD)((size - offsetof(MIB_TCP6TABLE_OWNER_PID, table))
        / sizeof(MIB_TCP6ROW_OWNER_PID));
    if (table->dwNumEntries < n)
        n = table->dwNumEntries;
    i = 0;
    while (i < n)
    {
        port = ntohs((u_short)table->table[i].dwLocalPort);
        attribute_port(tbl, table->table[i].dwOwningPid, port);
        i++;
    }
}

/*                                ENTRY POINT                                 */

/*
** Clears ports/port_count on every process first, unconditionally: stale
** ports from the previous sample would be actively misleading (a process
** that closed its listener still shown holding it), so this happens even
** if both table reads below fail. Then fills from the current IPv4 and
** IPv6 listener tables, deduplicated per process - a process listening
** on both stacks for the same port appears once.
**
** Code review finding: this used to return 0 only when BOTH families
** read successfully (ok == 2), and src/app.c's --selftest gates its exit
** code on that return value. IPv6 can be disabled outright on a machine
** (DisabledComponents in the registry, common on locked-down corporate
** images) with IPv4 working perfectly - GetExtendedTcpTable(AF_INET6)
** then fails not because anything is actually broken, but because the
** stack it is asking about does not exist. That turned a fully-working
** IPv4-only machine into "listening ports ... FAIL" and a nonzero
** --selftest exit code, contradicting docs/install.md's own "both should
** exit 0" - a false alarm indistinguishable from a real one.
**
** One working family is enough for the tool to do its job (attribute the
** ports that exist), so a partial read is no longer treated as a hard
** failure: this returns 0 when both families came back, 1 when exactly
** one did (partial - see platform.h), and -1 only when neither did. A
** caller that only cares "did this basically work" (app_sample(), which
** ignores the return value entirely and just uses whatever got filled in)
** can keep treating anything >= 0 as fine; app_selftest() is the one
** caller that distinguishes 0 from 1, so a machine with IPv6 disabled
** reports the partial state in its detail string instead of failing.
*/
int     plat_ports(t_table *tbl)
{
    void    *buf4;
    void    *buf6;
    ULONG   size4;
    ULONG   size6;
    int     ok;

    clear_all_ports(tbl);
    ok = 0;
    size4 = 0;
    buf4 = fetch_tcp_table(AF_INET, &size4);
    if (buf4 != NULL)
    {
        walk_tcp4(tbl, (const MIB_TCPTABLE_OWNER_PID *)buf4, size4);
        free(buf4);
        ok++;
    }
    size6 = 0;
    buf6 = fetch_tcp_table(AF_INET6, &size6);
    if (buf6 != NULL)
    {
        walk_tcp6(tbl, (const MIB_TCP6TABLE_OWNER_PID *)buf6, size6);
        free(buf6);
        ok++;
    }
    if (ok == 2)
        return (0);
    if (ok == 1)
        return (1);
    return (-1);
}
