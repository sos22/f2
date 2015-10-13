#include "job.H"
#include "logging.H"
#include "test2.H"

#include "parsers.tmpl"
#include "serialise.tmpl"
#include "test2.tmpl"

static testmodule __testjob(
    "job",
    list<filename>::mk("job.C", "job.H"),
    testmodule::LineCoverage(90_pc),
    testmodule::BranchCoverage(70_pc),
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
    "parser2", [] {
        logmsg(
            loglevel::notice,
            "res " +
            job::parser()
            .match(
                "<job:<filename:\"./testjob.so\">:testfunction -><stream:outstream>>")
            .fatal("parsingjob")
            .field()); },
    "convenience", [] {
        job j(filename("a"), string("b"));
        auto ss1(streamname::mk("1").fatal("1"));
        auto ss2(streamname::mk("2").fatal("2"));
        j.addoutput(ss1);
        assert(j == job(filename("a"),
                        string("b"),
                        empty,
                        list<streamname>::mk(ss1)));
        j.addoutput(ss2);
        assert(j == job(filename("a"),
                        string("b"),
                        empty,
                        list<streamname>::mk(ss1, ss2)));
        quickcheck q;
        auto jj(mkrandom<job>(q));
        j.addinput(ss1, jj.name(), ss2);
        assert(j == job(filename("a"),
                        string("b"),
                        map<streamname, job::inputsrc>(
                            ss1,
                            job::inputsrc(jj.name(), ss2)),
                        list<streamname>::mk(ss1, ss2)));
        auto jj2(mkrandom<job>(q));
        j.addinput(ss2, jj2.name(), ss1);
        assert(j == job(filename("a"),
                        string("b"),
                        map<streamname, job::inputsrc>(
                            ss1, job::inputsrc(jj.name(), ss2),
                            ss2, job::inputsrc(jj2.name(), ss1)),
                        list<streamname>::mk(ss1, ss2))); },
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
