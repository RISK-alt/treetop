/* tests/test_smoke.c */
#include "harness.h"
#include "treetop.h"

void    test_smoke(void)
{
    TT_EQ_WSTR(TT_VERSION, L"0.1.0");
    TT_EQ_INT(TT_MAX_PORTS, 8);
}
