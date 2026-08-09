#pragma once

/*                                  INCLUDES                                  */

# include "treetop.h"

/*                                 IDENTITY                                   */

/*
** A PID alone is not an identity on Windows: the OS reuses them within
** seconds. Everything - the command-line cache, the previous-sample
** lookup, the selection, the collapsed flag - keys on this pair.
*/
typedef struct s_proc_key
{
    unsigned long       pid;
    unsigned long long  create_time;
}   t_proc_key;

/*                                 PROCESS                                    */

typedef struct s_process
{
    t_proc_key          key;
    unsigned long       ppid;

    wchar_t             image[TT_IMAGE_LEN];
    const wchar_t       *cmdline;
    const wchar_t       *user;

    unsigned long long  kernel_time;
    unsigned long long  user_time;
    unsigned long long  working_set;
    unsigned long       thread_count;

    double              cpu_pct;
    double              mem_pct;

    unsigned short      ports[TT_MAX_PORTS];
    int                 port_count;

    int                 depth;
    int                 is_orphan;
    int                 is_agent_root;
    /*
    ** Set on every process inside an agent session, root included. The
    ** agents-only view needs the whole subtree - showing a session root
    ** with its children hidden would defeat the point of the view.
    */
    int                 in_session;
    const wchar_t       *agent_label;

    int                 collapsed;

    double              subtree_cpu;
    unsigned long long  subtree_mem;
    int                 subtree_count;

    struct s_process    *parent;
    struct s_process    *first_child;
    struct s_process    *next_sibling;
}   t_process;

/*                                  TABLE                                     */

typedef struct s_table
{
    t_process           *procs;
    size_t              count;
    size_t              capacity;
    unsigned long long  sample_time;
    unsigned int        core_count;
    unsigned long long  mem_total;
}   t_table;

int         table_init(t_table *tbl, size_t capacity);
void        table_free(t_table *tbl);
void        table_clear(t_table *tbl);
t_process   *table_add(t_table *tbl, const t_process *src);
t_process   *table_find(t_table *tbl, t_proc_key key);
t_process   *table_find_pid(t_table *tbl, unsigned long pid);
int         key_eq(t_proc_key a, t_proc_key b);

/*                                  DELTAS                                    */

void        delta_apply(t_table *cur, const t_table *prev);

/*                                   TREE                                     */

void        tree_build(t_table *tbl);
size_t      tree_flatten(t_table *tbl, t_process **out, size_t max);
void        tree_mark_orphans(t_table *tbl);
int         is_dev_runtime(const wchar_t *image);
