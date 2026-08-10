#include "input.h"

static int  contains_ci(const wchar_t *hay, const wchar_t *needle)
{
    size_t  i;
    size_t  j;

    if (hay == NULL || needle == NULL)
        return (0);
    for (i = 0; hay[i] != L'\0'; i++)
    {
        for (j = 0; needle[j] != L'\0'; j++)
            if (tt_lower(hay[i + j]) != tt_lower(needle[j]))
                break;
        if (needle[j] == L'\0')
            return (1);
    }
    return (0);
}

int     filter_match(const t_process *p, const wchar_t *needle)
{
    if (needle == NULL || needle[0] == L'\0')
        return (1);
    if (contains_ci(p->image, needle))
        return (1);
    return (contains_ci(p->cmdline, needle));
}
