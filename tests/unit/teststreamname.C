#include "streamname.H"
#include "test2.H"

#include "orerror.tmpl"
#include "parsers.tmpl"
#include "test2.tmpl"

static const testmodule __teststreamname(
    "streamname",
    list<filename>::mk("streamname.C", "streamname.H"),
    testmodule::BranchCoverage(52_pc),
    testmodule::LineCoverage(80_pc),
    "quickcheck", [] {
        /* Quickcheck should generate a valid name reasonably
         * quickly. */
        quickcheck q;
        deserialise1 ds(q);
        for (unsigned cntr = 0; true; cntr++) {
            streamname s(ds);
            if (!ds.isfailure()) break;
            assert(cntr < 10); } },
    "parsers", [] {
        auto &p(streamname::parser());
        auto bt([&p] (const char *what) {
                streamname sn(
                    streamname::mk(string(what))
                    .fatal("make streamname for " + fields::mk(what)));
                assert(p.match(sn.field().c_str()) == sn); });
        bt("hello");
        bt("x");
        parsers::roundtrip<streamname>(p); },
    "filename", [] {
        auto &p(streamname::filenameparser());
        /* Should also be able to parse the filename variant. */
        quickcheck q;
        for (unsigned x = 0; x < 100; x++) {
            deserialise1 ds(q);
            streamname sn(ds);
            auto r(p.match(sn.asfilename()));
            assert(r == sn); } }
);
