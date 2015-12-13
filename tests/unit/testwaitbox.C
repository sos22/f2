#include "spark.H"
#include "test2.H"
#include "testassert.H"
#include "timedelta.H"
#include "waitbox.H"

#include "spark.tmpl"
#include "testassert.tmpl"
#include "test2.tmpl"
#include "waitbox.tmpl"

static testmodule __testwaitbox(
    "waitbox",
    list<filename>::mk("waitbox.C", "waitbox.H", "waitbox.tmpl"),
    testmodule::BranchCoverage(50_pc),
    "setif", [] (clientio io) {
        waitbox<unsigned> wb;
        wb.set(5);
        wb.setif(6);
        wb.setif(7);
        assert(wb.ready());
        assert(wb.get(io) == 5); },
    "setif2", [] {
        waitbox<unsigned> wb;
        wb.setif(6);
        assert(wb.poll() == 6); },
    "void", [] {
        waitbox<void> wv;
        assert(wv.poll() == Nothing);
        wv.set();
        assert(wv.poll() == Void()); },
    "waitdeadline", [] (clientio io) {
        waitbox<int> foo;
        tassert(T2(timedelta,
                   timedelta::time([&] { foo.get(io, (100_ms).future()); } ))
                < T(500_ms));
        foo.set(5);
        assert(foo.get(io, (100_ms).future()) == 5); },
    "field", [] {
        waitbox<int> w1;
        tassert(T(string(w1.field().c_str())) ==
                T(string("Nothing")));
        w1.set(5);
        tassert(T(string(w1.field().c_str())) ==
                T(string("<5>"))); },
    "pub", [] {
        waitbox<int> wi;
        assert(wi.poll() == Nothing);
        subscriber s;
        subscription ss(s, wi.pub());
        assert(s.poll() == &ss);
        assert(s.poll() == NULL);
        wi.set(5);
        assert(s.poll() == &ss);
        assert(s.poll() == NULL);
        assert(wi.poll() == 5); },
    "wait", [] (clientio io) {
        waitbox<int> wi;
        spark<void> v([&] { assert(wi.get(io) == 7); });
        (50_ms).future().sleep(io);
        wi.set(7); } );
