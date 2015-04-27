#include <sys/time.h>
#include <err.h>
#include <signal.h>

#include "fields.H"
#include "percentage.H"
#include "logging.H"
#include "map.H"
#include "maybe.H"
#include "mutex.H"
#include "parsers.H"
#include "peername.H"
#include "percentage.H"
#include "profile.H"
#include "pubsub.H"
#include "serialise.H"
#include "spawn.H"
#include "storageconfig.H"
#include "string.H"
#include "test.H"
#include "thread.H"
#include "timedelta.H"
#include "walltime.H"

int
main(int argc, char *argv[])
{
    struct timeval now;
    gettimeofday(&now, NULL);
    printf("Seed: %lx\n", now.tv_usec);
    srandom((unsigned)now.tv_usec);

    /* Ones which are hard to convert to the new model: */
    tests::logging(); /* tests .C-local class */

    signal(SIGPIPE, SIG_IGN);

    while (argc > 1) {
        if (!strcmp(argv[1], "--verbose")) {
            initlogging("tests");
            argv++;
            argc--; }
        else if (!strcmp(argv[1], "--profile")) {
            startprofiling();
            argv++;
            argc--; }
        else break; }

    switch (argc) {
    case 1: tests::listcomponents(); break;
    case 2: tests::listtests(argv[1]); break;
    case 3: tests::runtest(argv[1], argv[2]); break;
    default: errx(1, "need zero, one, or two arguments"); }
    stopprofiling();
    return 0; }
