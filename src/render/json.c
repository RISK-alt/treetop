#include "render.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
** This whole file is portable C - no Win32, no WideCharToMultiByte. It
** lives in treetop_core specifically so treetop_tests can link and
** exercise it without a real Windows process table, and so a future
** port never has to touch the one place command lines turn into bytes.
*/

/*                                  ESCAPE                                    */

/*
** Emits a JSON-escaped copy of src into a wide buffer - still wchar_t,
** not yet UTF-8. json_utf8_encode() is the second stage that turns this
** into the bytes fwrite actually sees. Splitting the two steps keeps
** each one simple and independently testable, and lets escaping stay
** ignorant of encoding (a raw non-ASCII code unit, or half of a
** surrogate pair, passes straight through here unescaped - JSON only
** requires quotes, backslashes, and control characters to be escaped).
**
** Every escape sequence is written whole or not at all: before adding a
** sequence's units this checks they fit ahead of the terminating NUL, so
** truncation can only ever fall between two complete sequences, never
** inside one. A caller never sees a dangling "\" with its partner
** silently dropped, which would otherwise be invalid JSON.
*/
void    json_escape(const wchar_t *src, wchar_t *out, size_t n)
{
    size_t  i;
    size_t  o;
    wchar_t seq[8];
    size_t  w;

    if (n == 0)
        return ;
    if (src == NULL)
    {
        out[0] = L'\0';
        return ;
    }
    i = 0;
    o = 0;
    while (src[i] != L'\0')
    {
        if (src[i] == L'"')
        {
            seq[0] = L'\\';
            seq[1] = L'"';
            w = 2;
        }
        else if (src[i] == L'\\')
        {
            seq[0] = L'\\';
            seq[1] = L'\\';
            w = 2;
        }
        else if (src[i] == L'\n')
        {
            seq[0] = L'\\';
            seq[1] = L'n';
            w = 2;
        }
        else if (src[i] == L'\t')
        {
            seq[0] = L'\\';
            seq[1] = L't';
            w = 2;
        }
        else if (src[i] == L'\r')
        {
            seq[0] = L'\\';
            seq[1] = L'r';
            w = 2;
        }
        else if (src[i] < 0x20)
        {
            swprintf(seq, 8, L"\\u%04x", (unsigned int)src[i]);
            w = 6;
        }
        else
        {
            seq[0] = src[i];
            w = 1;
        }
        if (o + w > n - 1)
            break ;
        wmemcpy(out + o, seq, w);
        o += w;
        i++;
    }
    out[o] = L'\0';
}

/*                                   UTF-8                                    */

/*
** wchar_t is 16 bits on Windows, so a code point above U+FFFF arrives as
** a high/low surrogate pair that must be recombined before encoding. An
** unpaired surrogate - a lone high surrogate with no low surrogate after
** it, a lone low surrogate, or a high surrogate truncated right at the
** end of the string - is replaced with U+FFFD rather than encoded
** literally, which would produce bytes no UTF-8 reader accepts.
**
** Looking at src[i + 1] to test for a pairing never reads past the
** allocation: this branch is only reached when src[i] itself is not the
** terminating NUL, so src[i + 1] is either the next real character or
** that same NUL - both are valid reads within the string.
**
** Like json_escape(), a sequence is written whole or not at all, so
** truncation can only ever fall on a code-point boundary, never inside a
** multi-byte UTF-8 sequence.
*/
size_t  json_utf8_encode(const wchar_t *src, char *out, size_t n)
{
    size_t          i;
    size_t          o;
    unsigned int    hi;
    unsigned int    lo;
    unsigned int    cp;
    unsigned char   seq[4];
    size_t          w;

    if (n == 0)
        return (0);
    if (src == NULL)
    {
        out[0] = '\0';
        return (0);
    }
    i = 0;
    o = 0;
    while (src[i] != L'\0')
    {
        hi = (unsigned int)(unsigned short)src[i];
        if (hi >= 0xD800 && hi <= 0xDBFF)
        {
            lo = (unsigned int)(unsigned short)src[i + 1];
            if (lo >= 0xDC00 && lo <= 0xDFFF)
            {
                cp = 0x10000 + ((hi - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
            else
            {
                cp = 0xFFFD;
                i += 1;
            }
        }
        else if (hi >= 0xDC00 && hi <= 0xDFFF)
        {
            cp = 0xFFFD;
            i += 1;
        }
        else
        {
            cp = hi;
            i += 1;
        }
        if (cp < 0x80)
        {
            seq[0] = (unsigned char)cp;
            w = 1;
        }
        else if (cp < 0x800)
        {
            seq[0] = (unsigned char)(0xC0 | (cp >> 6));
            seq[1] = (unsigned char)(0x80 | (cp & 0x3F));
            w = 2;
        }
        else if (cp < 0x10000)
        {
            seq[0] = (unsigned char)(0xE0 | (cp >> 12));
            seq[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
            seq[2] = (unsigned char)(0x80 | (cp & 0x3F));
            w = 3;
        }
        else
        {
            seq[0] = (unsigned char)(0xF0 | (cp >> 18));
            seq[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
            seq[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
            seq[3] = (unsigned char)(0x80 | (cp & 0x3F));
            w = 4;
        }
        if (o + w > n - 1)
            break ;
        memcpy(out + o, seq, w);
        o += w;
    }
    out[o] = '\0';
    return (o);
}

/*                                   EMIT                                     */

static double   finite_or_zero(double v)
{
    if (!isfinite(v))
        return (0.0);
    return (v);
}

/*
** Writes a JSON string literal - quotes included - for a possibly-NULL
** wide field. NULL becomes the JSON literal null, never the four
** characters "(null)" and never a crash: cmdline and agent_label are
** both legitimately NULL (an unreadable command line, a process outside
** any agent session), and a consumer parsing this file needs to be able
** to tell that apart from an empty string.
**
** Buffers are sized off the source length rather than a fixed stack
** array: a Windows command line can run to tens of thousands of
** characters, and a fixed cap would silently mangle the tail of a long
** one instead of just not fitting it. Six wide units is the worst case
** per source unit (a \u00XX escape); four bytes is the worst case per
** escaped wide unit once UTF-8 encoded.
*/
static void write_wstr(FILE *out, const wchar_t *s)
{
    size_t  len;
    size_t  esc_cap;
    size_t  utf8_cap;
    wchar_t *esc;
    char    *utf8;

    if (s == NULL)
    {
        fputs("null", out);
        return ;
    }
    len = wcslen(s);
    esc_cap = len * 6 + 1;
    esc = malloc(esc_cap * sizeof(wchar_t));
    if (esc == NULL)
    {
        fputs("\"\"", out);
        return ;
    }
    json_escape(s, esc, esc_cap);
    utf8_cap = wcslen(esc) * 4 + 1;
    utf8 = malloc(utf8_cap);
    if (utf8 == NULL)
    {
        free(esc);
        fputs("\"\"", out);
        return ;
    }
    json_utf8_encode(esc, utf8, utf8_cap);
    fputc('"', out);
    fputs(utf8, out);
    fputc('"', out);
    free(esc);
    free(utf8);
}

static void write_ports(FILE *out, const t_process *p)
{
    int i;

    fputc('[', out);
    for (i = 0; i < p->port_count; i++)
    {
        if (i > 0)
            fputc(',', out);
        fprintf(out, "%u", (unsigned int)p->ports[i]);
    }
    fputc(']', out);
}

static void write_session(FILE *out, const t_process *p)
{
    fputs("    { \"label\": ", out);
    write_wstr(out, p->agent_label);
    fprintf(out, ", \"pid\": %lu, \"cpu_pct\": %.1f, \"mem\": %llu, "
            "\"process_count\": %d }", p->key.pid,
            finite_or_zero(p->subtree_cpu), p->subtree_mem,
            p->subtree_count);
}

static void write_orphan(FILE *out, const t_process *p)
{
    fprintf(out, "    { \"pid\": %lu, \"image\": ", p->key.pid);
    write_wstr(out, p->image);
    fputs(", \"cmdline\": ", out);
    write_wstr(out, p->cmdline);
    fputs(", \"ports\": ", out);
    write_ports(out, p);
    fprintf(out, ", \"cpu_pct\": %.1f, \"mem\": %llu }",
            finite_or_zero(p->cpu_pct), p->working_set);
}

static void write_process(FILE *out, const t_process *p)
{
    fprintf(out, "    { \"pid\": %lu, \"ppid\": %lu, \"image\": ",
            p->key.pid, p->ppid);
    write_wstr(out, p->image);
    fputs(", \"cmdline\": ", out);
    write_wstr(out, p->cmdline);
    fprintf(out, ", \"cpu_pct\": %.1f, \"mem\": %llu, \"threads\": %lu, "
            "\"ports\": ", finite_or_zero(p->cpu_pct), p->working_set,
            p->thread_count);
    write_ports(out, p);
    fprintf(out, ", \"orphan\": %s, \"agent_root\": %s, \"agent_label\": ",
            p->is_orphan ? "true" : "false",
            p->is_agent_root ? "true" : "false");
    write_wstr(out, p->agent_label);
    fputs(" }", out);
}

/*
** sessions and orphans are deliberately derived views over processes,
** duplicated rather than normalised away: a consumer asking "what did I
** leave running" should not have to reconstruct the tree to find out.
** A session's cpu/mem/process_count come from the agent root's
** subtree_* fields (tree_build() already aggregates a whole subtree onto
** its root), which is exactly "this agent and everything it spawned".
*/
void    json_emit(const t_table *tbl, const t_sysinfo *sys, FILE *out)
{
    time_t      now;
    struct tm   utc;
    char        ts[32];
    size_t      i;
    int         first;

    /*
    ** gmtime_s rather than gmtime: gmtime hands back a pointer into
    ** storage shared by the whole process, so its result is only stable
    ** until the next call from anywhere. Nothing in treetop makes a
    ** second call today, and nothing is threaded, so this is a property
    ** worth keeping rather than a bug being fixed - the failure it
    ** prevents is the one where a later change adds a caller and this
    ** timestamp silently becomes someone else's. The _s form writes into
    ** a local, which cannot be aliased, and returns non-zero rather than
    ** NULL on failure.
    */
    now = time(NULL);
    if (gmtime_s(&utc, &now) == 0)
        strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &utc);
    else
        strcpy(ts, "1970-01-01T00:00:00Z");
    fputs("{\n", out);
    fputs("  \"version\": ", out);
    write_wstr(out, TT_VERSION);
    fputs(",\n", out);
    fprintf(out, "  \"sampled_at\": \"%s\",\n", ts);
    fprintf(out, "  \"system\": { \"cores\": %u, \"cpu_pct\": %.1f, "
            "\"mem_total\": %llu, \"mem_used\": %llu },\n",
            sys->core_count, finite_or_zero(sys->cpu_pct), sys->mem_total,
            sys->mem_used);
    fputs("  \"sessions\": [\n", out);
    first = 1;
    for (i = 0; i < tbl->count; i++)
    {
        if (!tbl->procs[i].is_agent_root)
            continue ;
        if (!first)
            fputs(",\n", out);
        write_session(out, &tbl->procs[i]);
        first = 0;
    }
    if (!first)
        fputc('\n', out);
    fputs("  ],\n", out);
    fputs("  \"orphans\": [\n", out);
    first = 1;
    for (i = 0; i < tbl->count; i++)
    {
        if (!tbl->procs[i].is_orphan)
            continue ;
        if (!first)
            fputs(",\n", out);
        write_orphan(out, &tbl->procs[i]);
        first = 0;
    }
    if (!first)
        fputc('\n', out);
    fputs("  ],\n", out);
    fputs("  \"processes\": [\n", out);
    first = 1;
    for (i = 0; i < tbl->count; i++)
    {
        if (!first)
            fputs(",\n", out);
        write_process(out, &tbl->procs[i]);
        first = 0;
    }
    if (!first)
        fputc('\n', out);
    fputs("  ]\n", out);
    fputs("}\n", out);
}
