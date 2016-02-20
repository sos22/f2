#include <unistd.h>

#include "testassert.H"
#include "test2.H"
#include "timedelta.H"

#include "fields.tmpl"
#include "parsers.tmpl"
#include "serialise.tmpl"
#include "testassert.tmpl"
#include "test2.tmpl"
#include "timedelta.tmpl"

static testmodule __testtimedelta(
    "timedelta",
    list<filename>::mk("timedelta.C", "timedelta.H", "timedelta.tmpl"),
    testmodule::LineCoverage(85_pc),
    testmodule::BranchCoverage(65_pc),
    "parsers", [] { parsers::roundtrip(timedelta::parser()); },
    "algebra", [] {
        assert(timedelta::milliseconds(200) + timedelta::milliseconds(800)==
               timedelta::seconds(1));
        timestamp n(timestamp::now());
        assert(timedelta::seconds(1) + n ==
               n +
               timedelta::milliseconds(100) +
               timedelta::microseconds(800000) +
               timedelta::nanoseconds(100000000));
        assert(timedelta::seconds(1) / timedelta::milliseconds(10) == 100);
        assert(timedelta::milliseconds(100).as_milliseconds() == 100);
        assert(timedelta::milliseconds(5) < timedelta::milliseconds(10));
        assert(timedelta::seconds(1) - timedelta::milliseconds(1) ==
               timedelta::milliseconds(999));
        assert(timedelta::seconds(1) <= timedelta::seconds(1));
        assert(timedelta::seconds(1) <= timedelta::seconds(2));
        assert(!(timedelta::seconds(2) <= timedelta::seconds(1)));
        assert(timedelta::seconds(1) >= timedelta::seconds(1));
        assert(timedelta::seconds(2) >= timedelta::seconds(1));
        assert(!(timedelta::seconds(1) >= timedelta::seconds(2)));
        assert(timedelta::seconds(1) * 2 == timedelta::seconds(2));
        assert(timedelta::seconds(2) / 2 == timedelta::seconds(1));
        assert(2 * timedelta::milliseconds(500) ==
               timedelta::seconds(1));
        assert(timedelta::minutes(2) == timedelta::seconds(120));
        assert(timedelta::hours(1) == timedelta::minutes(60));
        assert(timedelta::days(2) == timedelta::hours(48));
        assert(timedelta::weeks(3) == timedelta::days(21));
        assert(timedelta::minutes(1) != timedelta::seconds(1));
        assert(!(timedelta::minutes(1) != timedelta::minutes(1)));
        assert(timedelta::hours(1) > timedelta::minutes(59)); },
    "astimespec", [] {
        auto td(timedelta::milliseconds(-100).astimespec());
        assert(td.tv_sec == -1);
        assert(td.tv_nsec == 900000000);
        td = timedelta::nanoseconds(-1).astimespec();
        assert(td.tv_sec == -1);
        assert(td.tv_nsec == 999999999);
        td = timedelta::nanoseconds(-999999999).astimespec();
        assert(td.tv_sec == -1);
        assert(td.tv_nsec == 1);
        td = timedelta::nanoseconds(1).astimespec();
        assert(td.tv_sec == 0);
        assert(td.tv_nsec == 1);
        td = (1_s + timedelta::nanoseconds(7)).astimespec();
        assert(td.tv_sec == 1);
        assert(td.tv_nsec == 7); },
    "randrange", [] {
        for (unsigned x = 0; x < 1000; x++) {
            timedelta a((quickcheck()));
            timedelta b((quickcheck()));
            if (a < b) {
                timedelta c(quickcheck(), a, b);
                assert(a <= c);
                assert(c < b); }
            else {
                timedelta c(quickcheck(), b, a);
                assert(b <= c);
                assert(c <= a);
                assert((c == a) == (a == b)); }
            assert(timedelta(quickcheck(), a, a) == a); } },
    "serialise", [] {
        quickcheck q;
        serialise<timedelta>(q); },
    "future", [] {
        quickcheck q;
        for (unsigned x = 0; x < 100; x++) {
            timedelta td(q);
            auto pls(td.future());
            assert(pls < timestamp::now() + td);
            assert(pls >
                   timestamp::now()+td-timedelta::milliseconds(10)); } },
    "time", [] {
        auto t(timedelta::time<int>([] {
                    (timestamp::now()+timedelta::milliseconds(100))
                        .sleep(clientio::CLIENTIO);
                    return 5; }));
        assert(t.v == 5);
        tassert(T(100_ms) < T(t.td));
        tassert(T(t.td) < T(timedelta::milliseconds(120))); },
    testmodule::TestFlags::valgrind(), "valgrindwarp", [] (clientio io) {
        /* Time works slightly different on the Valgrind timewarp. */
        auto td(timedelta::time([] {
                    /* Use the unistd.h sleep rather than timestamp
                     * sleep, so that this sleep doesn't get
                     * warped. */
                    sleep(1); }));
        tassert(T(td) > T((1_s / VALGRIND_TIMEWARP) / 2));
        tassert(T(td) < T((1_s / VALGRIND_TIMEWARP) * 2));
        td = timedelta::time([io] { (100_ms).future().sleep(io); });
        tassert(T(td) > T(100_ms));
        tassert(T(td) < T(400_ms)); },
    "timeV", [] {
        auto t(timedelta::time([] {
                    (100_ms).future().sleep(clientio::CLIENTIO); }));
        tassert(T(100_ms) < T(t));
        tassert(T(t) < T(120_ms)); });
