#include <sys/time.h>
#include <err.h>
#include <signal.h>

#include "beacontest.H"
#include "buffer.H"
#include "clustername.H"
#include "cond.H"
#include "connpool.H"
#include "either.H"
#include "error.H"
#include "fd.H"
#include "fields.H"
#include "filename.H"
#include "frequency.H"
#include "list.H"
#include "logging.H"
#include "lqueue.H"
#include "map.H"
#include "maybe.H"
#include "mutex.H"
#include "parsers.H"
#include "peername.H"
#include "profile.H"
#include "pubsub.H"
#include "serialise.H"
#include "spawn.H"
#include "storageconfig.H"
#include "string.H"
#include "test.H"
#include "thread.H"
#include "timedelta.H"
#include "waitqueue.H"
#include "walltime.H"

int
main(int argc, char *argv[])
{
    struct timeval now;
    gettimeofday(&now, NULL);
    printf("Seed: %lx\n", now.tv_usec);
    srandom((unsigned)now.tv_usec);

    tests::beacon();
    tests::_buffer();
    tests::__clustername();
    tests::cond();
    tests::_connpool();
    tests::either();
    tests::_eqtest();
    tests::_error();
    tests::fd();
    tests::fields();
    tests::_filename();
    tests::_frequency();
    tests::_list();
    tests::logging();
    tests::_lqueue();
    tests::_map();
    tests::_maybe();
    tests::mutex();
    tests::_orerror();
    tests::_pair();
    tests::parsers();
    tests::_peername();
    tests::pubsub();
    tests::_serialise();
    tests::_spawn();
    tests::__storageconfig();
    tests::_string();
    tests::thread();
    tests::_timedelta();
    tests::_waitqueue();
    tests::_walltime();

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
