#include "either.H"

#include <assert.h>

#include "test.H"

void
tests::either() {
    class tracklife {
    public: bool &destruct;
    public: tracklife(bool &construct, bool &_destruct)
        : destruct(_destruct) {
        assert(!construct);
        construct = true; }
    public: ~tracklife() {
        assert(!destruct);
        destruct = true; } };
    class copy {
    public: int counter;
    public: copy() : counter(5) {}
    public: copy(const copy &o) : counter(o.counter + 1) {} };
    testcaseV("either", "left", [] {
            auto x(::either<int, int>::left(7));
            assert(x.isleft());
            assert(x.left() == 7); });
    testcaseV("either", "right", [] {
            auto x(::either<int, int>::right(93));
            assert(x.isright());
            assert(x.right() == 93); });
    testcaseV("either", "cons", [] {
            {   ::either<int, const char *> x(left<const char *>(97));
                assert(x.isleft());
                assert(x.left() == 97); }
            {   ::either<const char *, int> x(right<const char *>(97));
                assert(x.isright());
                assert(x.right() == 97); } });
    testcaseV("either", "copyleft", [] {
            auto x(::either<copy, int>::left(copy()));
            ::either<copy, int> y(x);
            assert(y.left().counter == x.left().counter + 1); });
    testcaseV("either", "copyright", [] {
            auto x(::either<int, copy>::right(copy()));
            ::either<int, copy> y(x);
            assert(y.right().counter == x.right().counter + 1); });
    testcaseV("either", "leftlife", [] {
            bool cons = false;
            bool dest = false;
            {   auto x(::either<tracklife, int>::left(tracklife(cons, dest)));
                assert(cons);
                dest = false;
                assert(x.isleft());
                assert(!x.isright());
                x.left();
                assert(cons);
                assert(!dest); }
            assert(cons);
            assert(dest); });
    testcaseV("either", "rightlife", [] {
            bool cons = false;
            bool dest = false;
            {   auto x(::either<int, tracklife>::right(tracklife(cons, dest)));
                assert(cons);
                dest = false;
                assert(x.isright());
                assert(!x.isleft());
                x.right();
                assert(cons);
                assert(!dest); }
            assert(cons);
            assert(dest); });
    testcaseV("either", "=", [] {
            auto x(::either<int, int>::left(5));
            assert(x.left() == 5);
            x = ::either<int, int>::left(6);
            assert(x.left() == 6);
            x = ::either<int, int>::right(7);
            assert(x.right() == 7);
            x = ::either<int, int>::right(8);
            assert(x.right() == 8);
            x = ::either<int, int>::left(9);
            assert(x.left() == 9); });
    testcaseV("either", "mkleft", [] {
            bool cons = false;
            bool dest = false;
            auto x(::either<int, tracklife>::left(5));
            assert(!cons);
            assert(!dest);
            assert(x.left() == 5);
            x.mkleft(9);
            assert(!cons);
            assert(!dest);
            assert(x.left() == 9);
            x.mkright(cons, dest);
            assert(cons);
            assert(!dest);
            assert(x.isright());
            x.mkleft(11);
            assert(cons);
            assert(dest);
            assert(x.left() == 11);
            cons = false;
            dest = false;
            x.mkright(cons, dest);
            assert(cons);
            assert(!dest);
            assert(x.isright());
            cons = false;
            x.mkright(cons, dest);
            assert(cons);
            assert(dest);
            assert(x.isright());
            dest = false; }); }
