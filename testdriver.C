#include <err.h>
#include <signal.h>

#include "beacontest.H"
#include "buffer.H"
#include "cond.H"
#include "either.H"
#include "error.H"
#include "fd.H"
#include "fields.H"
#include "frequency.H"
#include "logging.H"
#include "masterconfig.H"
#include "maybe.H"
#include "mutex.H"
#include "parsers.H"
#include "peername.H"
#include "pubsub.H"
#include "ratelimiter.H"
#include "registrationsecret.H"
#include "test.H"
#include "thread.H"
#include "timedelta.H"

int
main(int argc, char *argv[])
{
    tests::beacon();
    tests::buffer();
    tests::cond();
    tests::either();
    tests::_error();
    tests::fd();
    tests::fields();
    tests::_frequency();
    tests::logging();
    tests::_masterconfig();
    tests::_maybe();
    tests::mutex();
    tests::parsers();
    tests::_peername();
    tests::pubsub();
    tests::ratelimiter();
    tests::_registrationsecret();
    tests::thread();
    tests::_timedelta();
    tests::wireproto();

    signal(SIGPIPE, SIG_IGN);

    switch (argc) {
    case 1: tests::listcomponents(); break;
    case 2: tests::listtests(argv[1]); break;
    case 3: tests::runtest(argv[1], argv[2]); break;
    default: errx(1, "need zero, one, or two arguments"); } }
