#include "bytecount.H"
#include "test2.H"

#include "parsers.tmpl"
#include "test2.tmpl"

static testmodule __bytecount(
    "bytecount",
    list<filename>::mk("bytecount.C", "bytecount.H"),
    testmodule::BranchCoverage(35_pc),
    "basics", [] {
        assert(1_B == bytecount::bytes(1));
        assert(1000_B == bytecount::kilobytes(1));
        assert(1024_B == bytecount::kibibytes(1));
        assert(1_MB == bytecount::megabytes(1));
        assert(1_MiB == bytecount::mebibytes(1));
        assert(10_B - 1_B == 9_B);
        assert(10_B >= 10_B);
        assert(10_B >= 9_B);
        assert(!(10_B > 10_B));
        assert(10_B > 9_B);
        assert(10_B - 9_B == 1_B);
        assert(10_B - 10_B == 0_B);
        assert(10_B - 11_B == Nothing);
        assert(1_B + 2_B == 3_B);
        assert(10_B / 2 == 5_B);
        assert(5_B * 2 == 10_B);
        assert(!strcmp((10_B).field().c_str(), "10B"));
        assert(bytecount::size('c') == 1_B); },
    "serialise", [] {
        quickcheck q;
        serialise<bytecount>(q); },
    "parser", [] { parsers::roundtrip<bytecount>(); },
    "parser2", [] {
        auto &b(bytecount::parser());
        assert(b.match("5") == 5_B);
        assert(b.match("5B") == 5_B);
        assert(b.match("4kB") == 4000_B);
        assert(b.match("4kiB") == 4096_B);
        assert(b.match("1MB") == 1_MB);
        assert(b.match("1MiB") == 1_MiB); });
