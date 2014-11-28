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
#include "maybe.H"
#include "mutex.H"
#include "parsers.H"
#include "peername.H"
#include "pubsub.H"
#include "serialise.H"
#include "spawn.H"
#include "storageconfig.H"
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
    tests::buffer();
    tests::__clustername();
    tests::cond();
    tests::_connpool();
    tests::either();
    tests::_error();
    tests::fd();
    tests::fields();
    tests::_filename();
    tests::_frequency();
    tests::_list();
    tests::logging();
    tests::_maybe();
    tests::mutex();
    tests::parsers();
    tests::_peername();
    tests::pubsub();
    tests::_serialise();
    tests::_spawn();
    tests::__storageconfig();
    tests::thread();
    tests::_timedelta();
    tests::_waitqueue();
    tests::_walltime();

    signal(SIGPIPE, SIG_IGN);

    if (!strcmp(argv[1], "--verbose")) {
        initlogging("tests");
        argv++;
        argc--; }

    switch (argc) {
    case 1: tests::listcomponents(); break;
    case 2: tests::listtests(argv[1]); break;
    case 3: tests::runtest(argv[1], argv[2]); break;
    default: errx(1, "need zero, one, or two arguments"); } }
