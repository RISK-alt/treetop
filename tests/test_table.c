/* tests/test_table.c */
#include "harness.h"
#include "process.h"

#include <string.h>

/* Shared by several suites: build a bare process record. */
t_process   mk_proc(unsigned long pid, unsigned long long ct,
                    unsigned long ppid, const wchar_t *image)
{
    t_process   p;

    memset(&p, 0, sizeof(p));
    p.key.pid = pid;
    p.key.create_time = ct;
    p.ppid = ppid;
    wcsncpy(p.image, image, TT_IMAGE_LEN - 1);
    return (p);
}

void    test_table(void)
{
    t_table     tbl;
    t_process   p;
    t_proc_key  k;
    size_t      i;

    TT_EQ_INT(table_init(&tbl, 4), 0);
    TT_EQ_INT((int)tbl.count, 0);

    p = mk_proc(100, 1000, 4, L"node.exe");
    TT_CHECK(table_add(&tbl, &p) != NULL);
    TT_EQ_INT((int)tbl.count, 1);

    /* Found by full key. */
    k.pid = 100; k.create_time = 1000;
    TT_CHECK(table_find(&tbl, k) != NULL);

    /* Same PID, different creation time: a different process entirely. */
    k.pid = 100; k.create_time = 9999;
    TT_CHECK(table_find(&tbl, k) == NULL);

    /* Growth past the initial capacity keeps every element. */
    for (i = 0; i < 50; i++)
    {
        p = mk_proc((unsigned long)(200 + i), 1000 + i, 100, L"x.exe");
        TT_CHECK(table_add(&tbl, &p) != NULL);
    }
    TT_EQ_INT((int)tbl.count, 51);
    k.pid = 249; k.create_time = 1049;
    TT_CHECK(table_find(&tbl, k) != NULL);

    /* clear keeps the buffer, resets the count. */
    table_clear(&tbl);
    TT_EQ_INT((int)tbl.count, 0);
    TT_CHECK(tbl.capacity >= 51);

    table_free(&tbl);
    TT_CHECK(tbl.procs == NULL);
}
