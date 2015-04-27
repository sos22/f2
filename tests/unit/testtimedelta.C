#include "test2.H"
#include "timedelta.H"

#include "parsers.tmpl"
#include "serialise.tmpl"
#include "test2.tmpl"
#include "timedelta.tmpl"

static testmodule __testtimedelta(
    "timedelta",
    list<filename>::mk("timedelta.C", "timedelta.H"),
    testmodule::LineCoverage(85_pc),
    testmodule::BranchCoverage(65_pc),
    "parsers", [] { parsers::roundtrip(parsers::_timedelta()); },
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
        assert(timedelta::milliseconds(100) < t.td);
        assert(t.td < timedelta::milliseconds(101)); },
    "timeV", [] {
        auto t(timedelta::time([] {
                    (timestamp::now()+timedelta::milliseconds(100))
                        .sleep(clientio::CLIENTIO); }));
        assert(timedelta::milliseconds(100) < t);
        assert(t < timedelta::milliseconds(101)); });