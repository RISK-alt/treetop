#include "treetop.h"

/*
** See treetop.h's own comment on tt_lower for why this is ASCII-only and
** why it is the one shared primitive underneath three otherwise distinct
** matchers (src/agent/detect.c, src/input/filter.c, src/model/tree.c).
*/
wchar_t tt_lower(wchar_t c)
{
    if (c >= L'A' && c <= L'Z')
        return ((wchar_t)(c + 32));
    return (c);
}
