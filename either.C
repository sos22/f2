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
    testcaseV("either", "quickcheck", [] {
            unsigned left = 0;
            unsigned right = 0;
            unsigned zerol = 0;
            unsigned zeror = 0;
            quickcheck q;
            for (unsigned x = 0; x < 10000; x++) {
                ::either<int, int> y(q);
                if (y.isleft()) {
                    left++;
                    if (y.left() == 0) zerol++; }
                if (y.isright()) {
                    right++;
                    if (y.right() == 0) zeror++; } }
            /* Reasonable balance between the two sides. */
            assert(left >= right / 4);
            assert(right >= left / 4);
            /* Some coverage of special values. */
            assert(zerol != 0);
            assert(zeror != 0); });
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
            dest = false; });
    testcaseV("either", "==", [] {
            quickcheck q;
            for (unsigned x = 0; x < 10000; x++) {
                ::either<int, int> l(q);
                ::either<int, int> r(q);
                assert((l == r) == !(l != r)); }
            assert((::either<int, char>(Left(), 5)) ==
                   (::either<int, char>(Left(), 5)));
            assert((::either<int, char>(Left(), 5)) !=
                   (::either<int, char>(Left(), 6)));
            assert((::either<int, char>(Left(), 5)) !=
                   (::either<int, char>(Right(), 'a')));
            assert((::either<int, int>(Left(), 5)) !=
                   (::either<int, int>(Right(), 5))); });
    testcaseV("either", "LeftRight", [] {
        ::either<int, char *> x(Left(), 5);
        assert(x.isleft());
        assert(x.left() == 5);
        ::either<int, const char *> y(Right(), "foo");
        assert(y.isright());
        assert(!strcmp(y.right(), "foo"));
        ::either<int, int> z(Left(), 7);
        assert(z.isleft());
        assert(z.left() == 7);
        ::either<int, int> w(Right(), 92);
        assert(w.isright());
        assert(w.right() == 92); }); }
