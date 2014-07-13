#include <err.h>

#include "beacontest.H"
#include "buffer.H"
#include "fields.H"
#include "logging.H"
#include "parsers.H"
#include "pubsub.H"
#include "test.H"

int
main(int argc, char *argv[])
{
    tests::beacon();
    tests::buffer();
    tests::fields();
    tests::logging();
    tests::parsers();
    tests::pubsub();
    tests::wireproto();

    switch (argc) {
    case 1: tests::listcomponents(); break;
    case 2: tests::listtests(argv[1]); break;
    case 3: tests::runtest(argv[1], argv[2]); break;
    default: errx(1, "need zero, one, or two arguments"); } }
