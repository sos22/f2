#include "job.H"
#include "test2.H"

#include "parsers.tmpl"
#include "serialise.tmpl"
#include "test2.tmpl"

static testmodule __testjob(
    "job",
    list<filename>::mk("job.C", "job.H"),
    testmodule::LineCoverage(55_pc),
    testmodule::BranchCoverage(55_pc),
    "serialise", [] {
        quickcheck q;
        serialise<job>(q); },
    "quickchecknodupes", [] {
        quickcheck q;
        unsigned succ(0);
        for (unsigned cntr = 0; succ < 100; cntr++) {
            assert(cntr < 10000);
            deserialise1 ds(q);
            job j(ds);
            if (ds.isfailure()) continue;
            succ++;
            assert(!j.outputs().hasdupes());
            assert(j.outputs().issorted()); } },
    "parser", [] { parsers::roundtrip<job>(); },
    "serialisename", [] {
        /* serialising and deserialising must produce something with
         * the same name.  We've had problems in the past with it
         * changing the hash table shape. */
        quickcheck q;
        for (unsigned cntr = 0; cntr < 1000; cntr++) {
            auto val(mkrandom<job>(q));
            buffer buf;
            {   serialise1 s(buf);
                val.serialise(s); }
            {   deserialise1 ds(buf);
                job res(ds);
                assert(ds.status().issuccess());
                assert(res == val);
                assert(res.name() == val.name()); } } }
    );
