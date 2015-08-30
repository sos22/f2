#include "either.H"
#include "test2.H"

#include "either.tmpl"
#include "serialise.tmpl"
#include "test2.tmpl"

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

static testmodule __testeither(
    "either",
    list<filename>::mk("either.H", "either.tmpl"),
    "left", [] {
        auto x(either<int, int>(Left(), 7));
        assert(x.isleft());
        assert(x.left() == 7); },
    "right", [] {
        auto x(either<int, int>(Right(), 93));
        assert(x.isright());
        assert(x.right() == 93); },
    "cons", [] {
        {   either<int, const char *> x(left<const char *>(97));
            assert(x.isleft());
            assert(x.left() == 97); }
        {   either<const char *, int> x(right<const char *>(97));
            assert(x.isright());
            assert(x.right() == 97); } },
    "copyleft", [] {
        either<copy, int> x((Left()), copy());
        either<copy, int> y((x));
        assert(y.left().counter == x.left().counter + 1); },
    "copyright", [] {
        either<int, copy> x((Right()), copy());
        either<int, copy> y((x));
        assert(y.right().counter == x.right().counter + 1); },
    "leftlife", [] {
        bool cons = false;
        bool dest = false;
        {   auto x(either<tracklife, int>(Left(), tracklife(cons, dest)));
            assert(cons);
            dest = false;
            assert(x.isleft());
            assert(!x.isright());
            x.left();
            assert(cons);
            assert(!dest); }
        assert(cons);
        assert(dest); },
    "rightlife", [] {
        bool cons = false;
        bool dest = false;
        {   either<int, tracklife> x((Right()), tracklife(cons, dest));
            assert(cons);
            dest = false;
            assert(x.isright());
            assert(!x.isleft());
            x.right();
            assert(cons);
            assert(!dest); }
        assert(cons);
        assert(dest); },
    "quickcheck", [] {
        unsigned left = 0;
        unsigned right = 0;
        unsigned zerol = 0;
        unsigned zeror = 0;
        quickcheck q;
        for (unsigned x = 0; x < 10000; x++) {
            either<int, int> y(q);
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
        assert(zeror != 0); },
    "=", [] {
        auto x(either<int, int>(Left(), 5));
        assert(x.left() == 5);
        x = either<int, int>(Left(), 6);
        assert(x.left() == 6);
        x = either<int, int>(Right(), 7);
        assert(x.right() == 7);
        x = either<int, int>(Right(), 8);
        assert(x.right() == 8);
        x = either<int, int>(Left(), 9);
        assert(x.left() == 9); },
    "=dest", [] {
        /* Something with a non-trivial destructor, to make sure that
         * works. This is only likely to fail under Valgrind. */
        {   either<string, int> e1(Left(), "hello");
            either<string, int> e2(Right(), 5);
            e1 = e2;
            assert(e1.isright());
            assert(e1.right() == 5);
            e1.mkleft("foo");
            e2 = e1;
            assert(e2.isleft());
            assert(e2.left() == "foo"); }
        {   either<int, string> e1(Right(), "hello");
            either<int, string> e2(Left(), 5);
            e1 = e2;
            assert(e1.isleft());
            assert(e1.left() == 5);
            e1.mkright("foo");
            e2 = e1;
            assert(e2.isright());
            assert(e2.right() == "foo"); } },
    "mkleft", [] {
        bool cons = false;
        bool dest = false;
        auto x(either<int, tracklife>(Left(), 5));
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
        dest = false; },
    "==", [] {
        quickcheck q;
        for (unsigned x = 0; x < 10000; x++) {
            either<int, int> l(q);
            either<int, int> r(q);
            assert((l == r) == !(l != r)); }
        assert((either<int, char>(Left(), 5)) ==
               (either<int, char>(Left(), 5)));
        assert((either<int, char>(Left(), 5)) !=
               (either<int, char>(Left(), 6)));
        assert((either<int, char>(Left(), 5)) !=
               (either<int, char>(Right(), 'a')));
        assert((either<int, int>(Left(), 5)) !=
               (either<int, int>(Right(), 5))); },
    "LeftRight", [] {
        either<int, char *> x(Left(), 5);
        assert(x.isleft());
        assert(x.left() == 5);
        either<int, const char *> y(Right(), "foo");
        assert(y.isright());
        assert(!strcmp(y.right(), "foo"));
        either<int, int> z(Left(), 7);
        assert(z.isleft());
        assert(z.left() == 7);
        either<int, int> w(Right(), 92);
        assert(w.isright());
        assert(w.right() == 92); },
    "serialise", [] {
        quickcheck q;
        serialise<either<int, bool> >(q); },
    "leftvoid", [] {
        {   either<void, int> e((Left()));
            assert(e.isleft());
            e.mkright(5);
            assert(e.isright());
            assert(e.right() == 5); }
        {   either<void, int> e(Right(), 7);
            assert(e.right() == 7); }
        assert((either<void, int>(Left()).isleft()));
        assert((either<void, int>(Right(), 6).isright()));
        assert((either<void, int>(Right(), 6).right() == 6)); },
    "rightvoid", [] {
        {   either<int, void> e((Right()));
            assert(e.isright());
            e.mkleft(5);
            assert(e.isleft());
            assert(e.left() == 5); }
        {   either<int, void> e(Left(), 7);
            assert(e.left() == 7); }
        assert((either<int, void>(Right()).isright()));
        assert((either<int, void>(Left(), 6).isleft()));
        assert((either<int, void>(Left(), 6).left() == 6)); },
    "steal", [] {
        {   either<string, string> a(Left(), "hello");
            either<string, string> b(Steal, a);
            assert(b.isleft());
            assert(b.left() == "hello");
            assert(a.isleft());
            assert(a.left() == ""); }
        {   either<string, string> a(Right(), "hello");
            either<string, string> b(Steal, a);
            assert(b.isright());
            assert(b.right() == "hello");
            assert(a.isright());
            assert(a.right() == ""); } } );
