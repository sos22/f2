#include "test.H"

#include <stdarg.h>
#include <stdio.h>

void
test::msg(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

void
test::detail(const char *, ...)
{
}
