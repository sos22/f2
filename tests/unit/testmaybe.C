#include "maybe.H"
#include "test2.H"

#include "either.tmpl"
#include "maybe.tmpl"
#include "parsers.tmpl"
#include "serialise.tmpl"
#include "test2.tmpl"

class nodestruct { public: ~nodestruct() { abort(); } };

class countcopies {
public: int counter;
public: explicit countcopies(int base) : counter(base) {}
public: countcopies(const countcopies &c) : counter(c.counter+1) {} };

class notedestruct {
public: bool *d;
public: notedestruct(bool *_d) : d(_d) { assert(*d == false); }
public: ~notedestruct() { *d = true; } };

class cons1 {
public: int val1;
public: const char *val2;
public: cons1(int _val1, const char *_val2) : val1(_val1), val2(_val2) {} };

static testmodule __testmaybe(
    "maybe",
    list<filename>::mk("maybe.C", "maybe.H", "maybe.tmpl"),
    testmodule::LineCoverage(90_pc),
    testmodule::BranchCoverage(50_pc),
    "nothing", [] { maybe<nodestruct> x(Nothing); },
    "copy", [] {
        maybe<countcopies> x(countcopies(0));
        maybe<countcopies> y(x);
        assert(x.just().counter == 1);
        assert(y.just().counter == 2); },
    "destruct", [] {
        bool dead = false;
        {   maybe<notedestruct> aaa = notedestruct(&dead);
            assert(aaa.just().d == &dead);
            dead = false; }
        assert(dead); },
    "assign", [] {
        bool dead = false;
        maybe<notedestruct> a = notedestruct(&dead);
        dead = false;
        a.mknothing();
        assert(dead == true); },
    "assign2", [] {
        maybe<int> a(7);
        a = 8;
        assert(a.just() == 8); },
    "consconst", [] {
        int x = 7;
        const int &y(x);
        maybe<int> z(y);
        assert(z.just() == 7); },
    "isnothing", [] {
        assert(!maybe<int>(Nothing));
        assert(!!maybe<int>(7)); },
    "dflt", [] {
        assert(maybe<int>(Nothing).dflt(8) == 8);
        assert(maybe<int>(9).dflt(8) == 9); },
    "!=", [] {
        assert(maybe<int>(7) != Nothing);
        assert(Nothing != maybe<int>(7));
        assert(maybe<int>(7) != maybe<int>(8));
        assert(maybe<int>(Nothing) == Nothing);
        assert(Nothing == maybe<int>(Nothing));
        assert(maybe<int>(7) == maybe<int>(7));
        assert(maybe<int>(7) != 8);
        assert(!(maybe<int>(7) != 7));
        assert(7 == maybe<int>(7));
        assert(!(7 == maybe<int>(Nothing)));
        assert(!(8 == maybe<int>(7))); },
    "map", [] {
        assert(maybe<int>(7)
               .map<char>([] (const int &x) { return x == 7 ? 'Y' : 'N'; })
               == 'Y');
        assert(maybe<int>(8)
               .map<char>([] (const int &x) { return x == 7 ? 'Y' : 'N'; })
               == 'N');
        assert(maybe<int>(Nothing)
               .map<char>([] (const int &x) { return x == 7 ? 'Y' : 'N'; })
               == Nothing); },
    "void", [] {
        assert(maybe<void>(Nothing) == Nothing);
        assert(Nothing == maybe<void>(Nothing));
        assert(maybe<void>::just != Nothing);
        assert(maybe<void>::just == maybe<void>(maybe<void>::just));
        maybe<void> x(Nothing);
        assert(x == Nothing);
        x = maybe<void>::just;
        assert(x != Nothing); },
    "qc", [] {
        bool havenothing = false;
        bool haveanyjust = false;
        bool havediffjust = false;
        unsigned firstjust;
        quickcheck q;
        for (unsigned x = 0; x < 20; x++) {
            maybe<unsigned> y(q);
            if (y == Nothing) {
                havenothing = true; }
            else if (!haveanyjust) {
                haveanyjust = true;
                firstjust = y.just(); }
            else if (firstjust != y.just()) {
                havediffjust = true; } }
        assert(havenothing);
        assert(haveanyjust);
        assert(havediffjust); },
    "=", [] {
        maybe<int> x(7);
        maybe<int> y(Nothing);
        y = x;
        assert(y == 7);
        x = Nothing;
        assert(x == Nothing);
        x = 9;
        assert(x == 9);
        x = y;
        assert(x == 7);
        y = Nothing;
        assert(y.isnothing());
        x = y;
        assert(x == Nothing); },
    "mkjust", [] {
        maybe<cons1> c(Nothing);
        const char *s = "Hello";
        c.mkjust(71, s);
        assert(c.isjust());
        assert(c.just().val1 == 71);
        assert(c.just().val2 == s); },
    "mkjust2", [] { assert(mkjust<int>(5) == 5); },
    "field", [] {
        assert(!strcmp(maybe<string>(Nothing).field().c_str(),
                       "Nothing"));
        assert(!strcmp(maybe<string>("foo").field().c_str(),
                       "<foo>")); },
    "serialise", [] {
        quickcheck q;
        serialise<maybe<unsigned> >(q);
        serialise<maybe<string> >(q); },
    "parser", [] {
        parsers::roundtrip(
            parsers::_maybe(parsers::intparser<unsigned>())); },
    "alignment", [] {
        maybe<unsigned long> x[3] = {1, 7, 9};
        assert((unsigned long)&x[0].just() % alignof(unsigned long) == 0);
        assert((unsigned long)&x[1].just() % alignof(unsigned long) == 0);
        assert((unsigned long)&x[2].just() % alignof(unsigned long) == 0); },
    "=nothing", [] {
        /* Should be able to go maybe<x> = Nothing even if x
         * doesn't support operator=. */
        class noeq {
        private: void operator=(const noeq &) = delete; };
        maybe<noeq> x(Nothing);
        assert(x.isnothing());
        x.mkjust();
        assert(x.isjust());
        x = Nothing;
        assert(x.isnothing()); },
    "assigndestruct", [] {
        bool dead = false;
        maybe<notedestruct> x(Nothing);
        maybe<notedestruct> y(Nothing);
        y.mkjust(&dead);
        assert(!dead);
        y = x;
        assert(dead); },
    "maybevoid", [] {
        assert(maybe<void>(Nothing).isnothing());
        auto just(maybe<void>::just);
        assert(just.isjust());
        maybe<void> x(Nothing);
        assert(x == x);
        assert(!(x == just));
        assert(just == just);
        x = just;
        assert(x.isjust()); },
    "Just", [] {
        maybe<int> x(Just(), 5);
        assert(x.just() == 5); });
