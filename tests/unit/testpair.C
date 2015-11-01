#include "pair.H"
#include "test2.H"

#include "serialise.tmpl"
#include "pair.tmpl"
#include "parsers.tmpl"
#include "test2.tmpl"

static testmodule __testpair(
    "pair",
    list<filename>::mk("pair.H", "pair.tmpl"),
    testmodule::LineCoverage(75_pc),
    "parser", [] {
        parsers::roundtrip<pair<int, int> >();
        parsers::roundtrip<pair<string, string> >(); },
    "serialise", [] {
        quickcheck q;
        serialise<pair<int, int> >(q);
        serialise<pair<int, string> >(q); },
    "operators", [] {
        assert((pair<int, int>(5, 7) == pair<int, int>(5, 7)));
        assert((pair<int, int>(5, 7) != pair<int, int>(5, 8)));
        assert((pair<int, int>(5, 7) != pair<int, int>(6, 7)));
        assert((pair<int, int>(1, 2) < pair<int, int>(1, 3)));
        assert((pair<int, int>(1, 100) < pair<int, int>(2, 0)));
        assert((pair<int, int>(1, 100) <= pair<int, int>(1, 100))); },
    "steal", [] {
        class cons {
        public: int &nrsteals;
        public: explicit cons(int &_nrsteals) : nrsteals(_nrsteals) {}
        public: cons() = delete;
        public: cons(_Steal, const cons &o)
            : nrsteals(o.nrsteals) {
            nrsteals++; } };
        {   int n(0);
            pair<cons, cons> a((cons(n)), (cons(n)));
            assert(n == 0);
            pair<cons, cons> b(Steal, a, Steal);
            assert(n == 2); }
        {   int n(0);
            pair<cons, cons> a((cons(n)), (cons(n)));
            pair<cons, cons> b(Steal, a);
            assert(n == 1); }
        {   int n(0);
            pair<cons, cons> a((cons(n)), (cons(n)));
            pair<cons, cons> b(a, Steal);
            assert(n == 1); }
        {   int n(0);
            pair<int, cons> a(5, cons(n));
            assert(n == 0);
            pair<int, cons> b(a, Steal);
            assert(n == 1); }});
