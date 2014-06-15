#include <err.h>
#include <string.h>

#include "buffer.H"
#include "fields.H"
#include "logging.H"
#include "test.H"

int
main(int argc, char *argv[])
{
    test t;

    if (argc != 2)
        errx(1, "need a single argument, the test to run");
    if (!strcmp(argv[1], "buffer"))
        buffer::test(t);
    else if (!strcmp(argv[1], "fields"))
        fields::test(t);
    else if (!strcmp(argv[1], "logging"))
        logtest(t);
    else
        errx(1, "unknown test %s", argv[1]);
    return 0;
}
