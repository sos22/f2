#include "map.H"
#include "test2.H"
#include "timedelta.H"

#include "fields.tmpl"
#include "map.tmpl"
#include "parsers.tmpl"
#include "serialise.tmpl"
#include "test2.tmpl"
#include "timedelta.tmpl"

template <typename _k, typename _v, unsigned long _h(const _k &)>
class testmap : public map<_k, _v, _h> {
public: typedef map<_k, _v, _h> base;
public: typedef testmap<_k, _v, _h> thisT;
public: template <typename ... args> testmap(args &&... params)
    : base(std::forward<args>(params)...) { }
public: unsigned &nrbuckets() { return base::nrbuckets; }
public: unsigned &nritems() { return base::nritems; }
public: static bool isprime(unsigned x) { return base::isprime(x); }
public: static unsigned nextsize(unsigned x) { return base::nextsize(x); }
public: static unsigned prevsize(unsigned x) { return base::prevsize(x); }
public: void rehash(unsigned x) { base::rehash(x); } };

static testmodule __testmap(
    "map",
    list<filename>::mk("map.C", "map.H", "map.tmpl"),
    testmodule::LineCoverage(90_pc),
    testmodule::BranchCoverage(60_pc),
    "basics", [] {
        testmap<int, string> m;
        assert(m.get(5) == Nothing);
        m.set(5, "hello");
        assert(m.get(5) == "hello");
        m.set(6, "goodbye");
        assert(m.get(5) == "hello");
        assert(m.get(6) == "goodbye");
        m.clear(6);
        assert(m.get(6) == Nothing);
        m.set(6, "dead");
        assert(m.get(6) == "dead");
        assert(m.get(5) == "hello");
        testmap<int, string> m2(m);
        assert(m.get(5) == "hello");
        assert(m.get(6) == "dead");
        assert(m2.get(5) == "hello");
        assert(m2.get(6) == "dead");
        m2.clear(5);
        assert(m.get(5) == "hello");
        assert(m.get(6) == "dead");
        assert(m2.get(5) == Nothing);
        assert(m2.get(6) == "dead");
        testmap<int, string> m3(Steal, m);
        assert(m.get(5) == Nothing);
        assert(m.get(6) == Nothing);
        assert(m3.get(5) == "hello");
        assert(m3.get(6) == "dead"); },
    "isprime", [] {
        typedef testmap<int, int> iT;
        /* Exhaustive for small numbers */
        assert(!iT::isprime(0));
        assert(!iT::isprime(1));
        assert(iT::isprime(2));
        assert(iT::isprime(3));
        assert(!iT::isprime(4));
        assert(iT::isprime(5));
        assert(!iT::isprime(6));
        assert(iT::isprime(7));
        assert(!iT::isprime(8));
        assert(!iT::isprime(9));
        assert(!iT::isprime(10));
        /* And pick a few arbitrary bigger ones */
        assert(iT::isprime(9623));
        assert(!iT::isprime(9624));
        assert(iT::isprime(99787));
        assert(!iT::isprime(99788));
        assert(!iT::isprime(999982));
        assert(iT::isprime(999983));
        assert(!iT::isprime(999984));
        assert(!iT::isprime(11715730));
        assert(iT::isprime(11715731));
        assert(!iT::isprime(11715732));
        assert(!iT::isprime(1003061836));
        assert(iT::isprime(1003061837));
        assert(!iT::isprime(1003061838)); },
    "nextsize", [] {
        typedef testmap<int, int> iT;
        unsigned c = 0;
        for (unsigned x = 0; x < 9; x++) {
            unsigned n = iT::nextsize(c);
            if (c != 0) assert(c == iT::prevsize(n));
            else assert(iT::prevsize(n) > 0);
            /* Aiming for a factor of eight each time. */
            assert(n > c * 7);
            if (c != 0) assert(n < c * 9);
            assert(iT::isprime(n));
            c = n; } },
    "collisions", [] {
        /* Engineer a lot of collisions in a small table (because we
         * know the hash function). */
        testmap<int, int> m;
        for (unsigned x = 1; x < 5; x++) {
            m.set(x * 7, x * 3);
            for (unsigned y = 1; y <= x; y++) {
                assert(m.get(y * 7) == y * 3); } }
        unsigned nr = 0;
        for (auto it(m.start()); !it.finished(); it.next()) {
            assert(nr < 5);
            assert(m.get(it.key()) == it.value());
            nr++; } },
    "growshrink", [] {
        /* Insert a load of entries in the table, so that it grows,
         * and then take them back out again, so that it shrinks. */
        testmap<int, long> m;
        for (unsigned x = 0; x < 1000000; x++) {
            assert(m.get(x) == Nothing);
            m.set(x, x+1);
            assert(m.get(x) == x+1);
            assert(m.nritems() == x+1);
            assert(m.nrbuckets() >= x / 10);
            assert(m.nrbuckets() <= x * 10 + 10); }
        for (unsigned x = 0; x < 1000000; x++) {
            m.clear(x);
            assert(m.get(x) == Nothing);
            assert(m.nritems() == 999999 - x);
            assert(m.nrbuckets() >= m.nritems() / 10);
            assert(m.nrbuckets() <= m.nritems() * 10 + 10); } },
    "immediate", [] {
        testmap<int, int> m(5,6, 10,11);
        assert(m.get(5) == 6);
        assert(m.get(10) == 11);
        assert(m.get(11) == Nothing); },
    "==", [] {
        typedef testmap<int, int> T;
        assert(T() == T());
        assert(T() != T(1,2));
        assert(T(1,2) == T(1,2));
        assert(T(1,2) != T(1,3));
        assert(T(1,2, 3,4) == T(1,2, 3,4));
        assert(T(1,2, 3,4) == T(3,4, 1,2));
        /* Hash collission */
        assert(T(1,2, 8,3) == T(1,2, 8,3));
        assert(T(1,2, 8,3) != T(1,2, 8,2));
        assert(T(1,2, 8,3) != T(1,2));
        /* Check that it works when they have different nrbuckets */
        T m1;
        for (unsigned x = 0; x < 100; x++) {
            unsigned k;
            do { k = (int)random(); } while (m1.get(k) != Nothing);
            m1.set(k, (int)random()); }
        T m2(m1);
        assert(m2.nrbuckets() != 23);
        m2.rehash(23);
        assert(m1 == m2);
        m2.set(53, m2.get(53).dflt(5)+3);
        assert(m1 != m2); },
    "strings", [] {
        assert(default_hashfn(string("hello")) !=
               default_hashfn(string("goodbye")));
        assert(default_hashfn(string("hello")) ==
               default_hashfn(string("hello")));
        testmap<string, string> m("hello", "bob",
                                  "goodbye", "bob",
                                  "greetings", "charlie");
        assert(m.get("hello") == "bob");
        assert(m.get("goodbye") == "bob");
        assert(m.get("greetings") == "charlie");
        m.clear("hello");
        assert(m == (map<string, string>("goodbye", "bob",
                                         "greetings", "charlie"))); },
    "iterator", [] {
        testmap<int, int> m(5, 7, 1, 2, 3, 4);
        bool a(false);
        bool b(false);
        bool c(false);
        for (auto it(m.start()); !it.finished(); it.next()) {
            if (it.key() == 5) {
                assert(!a);
                assert(it.value() == 7);
                a = true; }
            else if (it.key() == 1) {
                assert(!b);
                assert(it.value() == 2);
                b = true; }
            else if (it.key() == 3) {
                assert(!c);
                assert(it.value() == 4);
                c = true; }
            else abort(); }
        assert(a);
        assert(b);
        assert(c); },
    "intperf", [] {
        static const unsigned nr = 10'000'000;
        auto m = new testmap<int, int>();
        auto build(timedelta::time([m] {
                    for (unsigned x = 0; x < nr; x++) {
                        m->set(x * 1000117, x); } }));
        printf("build %s\n", fields::mk(build).c_str());
        auto scan1(timedelta::time([m] {
                    for (unsigned x = 0; x < nr; x++) {
                        m->get(x * 1000117); } }));
        printf("scan1 %s\n", fields::mk(scan1).c_str());
        auto scan2(timedelta::time([m] {
                    for (unsigned x = 0; x < nr; x++) {
                        m->get(((x * 10000229) % nr) * 1000117); } }));
        printf("scan2 %s\n", fields::mk(scan2).c_str());
        auto scan3(timedelta::time([m] {
                    for (unsigned x = 0; x < nr; x++) {
                        m->get(x * 1000117); } }));
        printf("scan3 %s\n", fields::mk(scan3).c_str());
        auto scan4(timedelta::time([m] {
                    for (unsigned x = 0; x < nr; x++) {
                        m->get(((x * 20000059) % nr) * 1000117); } }));
        printf("scan4 %s\n", fields::mk(scan4).c_str());
        auto destroy(timedelta::time([m] { delete m; }));
        printf("destroy %s\n", fields::mk(destroy).c_str());
        printf(
            "total %s\n",
            fields::mk(build+scan1+scan2+scan3+scan4+destroy).c_str()); },
    "field", [] {
#define tst(expected, ...)                                              \
        {   auto r = testmap<int, int>(__VA_ARGS__).field().c_str();    \
            if (strcmp(expected, r)) {                                  \
                fprintf(stderr, "wanted %s, got %s\n",                  \
                        expected,                                       \
                        r);                                             \
                abort(); } }
        tst("{}");
        tst("{1=>2}", 1, 2);
        tst("{1=>2;3=>4}", 1, 2, 3, 4);
#undef tst
        assert(!strcmp("{}", testmap<string, int>().field().c_str()));
        assert(!strcmp("{foo=>3}",
                       testmap<string, int>("foo", 3).field().c_str()));
        assert(!strcmp("{\"}\"=>3}",
                       testmap<string, int>("}", 3).field().c_str()));
        assert(!strcmp("{\"\\\"\"=>3}",
                       testmap<string, int>("\"", 3).field().c_str()));
        assert(!strcmp("{\"\\\"\"=>\"->\"}",
                       testmap<string, string>("\"", "->").field().c_str()));},
    "parser", [] {
        parsers::roundtrip<map<int, int> >();
        parsers::roundtrip<map<string, string> >(); },
    "serialise", [] {
        quickcheck q;
        /* The random map generator is expensive enough that we can't
         * just accept the usual 1000 iterations (especially when it's
         * full of strings). */
        serialise<testmap<int, int> >(q, 100);
        serialise<testmap<string, int> >(q, 10); },
    "quickcheck", [] {
        unsigned nrempty(0);
        unsigned zerokey(0);
        quickcheck q;
        for (unsigned x = 0; x < 10000; x++) {
            testmap<int, int> m(q);
            if (m.nritems() == 0) nrempty++;
            if (m.get(0) != Nothing) zerokey++; }
        assert(nrempty != 0);
        assert(nrempty < 1200);
        assert(zerokey > 10);
        assert(zerokey < 900); });
