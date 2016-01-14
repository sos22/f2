#include "list.H"
#include "test2.H"
#include "timedelta.H"
#include "util.H"

#include "fields.tmpl"
#include "orerror.tmpl"
#include "pair.tmpl"
#include "parsers.tmpl"
#include "serialise.tmpl"
#include "spark.tmpl"
#include "test2.tmpl"

namespace {
class withparser {
public: int val;
public: withparser(int what) : val(what) {}
public: bool operator==(withparser o) const { return val == o.val; }
public: static const ::parser<withparser> &parser() {
    return strmatcher("foo", withparser(1)) ||
        strmatcher("bar", withparser(2)); } }; }

static testmodule __listtest(
    "list",
    list<filename>::mk("list.H", "list.tmpl"),
    "pushtail", [] {
        list<int> l;
        l.pushtail(5,6,7,8);
        assert(l.pophead() == 5);
        assert(l.poptail() == 8);
        l.pushtail(9);
        assert(l.pophead() == 6);
        assert(l.pophead() == 7);
        assert(l.pophead() == 9);
        assert(l.empty()); },
    "mk", [] {
        auto l(list<int>::mk(5,6,7,8));
        assert(l.pophead() == 5);
        assert(l.pophead() == 6);
        assert(l.pophead() == 7);
        assert(l.pophead() == 8);
        assert(l.empty()); },
    "contains", [] {
        assert(!list<int>::mk().contains(7));
        assert(!list<int>::mk().contains(0));
        assert(!list<int>::mk(1).contains(0));
        assert(!list<int>::mk(1,2,3).contains(0));
        assert(list<int>::mk(1,2,3).contains(1));
        assert(list<int>::mk(1,2,3).contains(2));
        assert(list<int>::mk(1,2,3).contains(3));
        auto l(list<int>::mk(1,2,3));
        l.pophead();
        assert(!l.contains(1)); },
    "mklist", [] {
        auto l(mklist(1,2,3));
        assert(l == list<int>::mk(1,2,3)); },
    "sort", [] {
        {   list<int> emptylist;
            sort(emptylist);
            assert(emptylist.empty()); }
        auto one(mklist(5));
        sort(one);
        assert(one == mklist(5));
        auto two1(mklist(5, 6));
        sort(two1);
        assert(two1 == mklist(5, 6));
        auto two2(mklist(6, 5));
        sort(two2);
        assert(two2 == mklist(5, 6));
        auto two3(mklist(5, 6));
        sort<int>(two3, [] (const int &a, const int &b) { return a < b; });
        assert(two3 == mklist(6, 5)); },
    "=", [] {
        auto l1(mklist(5,6,7,8));
        l1 = list<int>::mk();
        assert(l1.empty());
        l1 = mklist(1,2,3);
        assert(l1 == mklist(1,2,3));
        l1 = mklist(4,5);
        assert(l1 == mklist(4,5)); },
    "partial", [] {
        list<string> l;
        auto p(l.mkpartial("foo"));
        p->val().truncate(1);
        auto p2(l.mkpartial("bar"));
        l.pushtail(p);
        auto p3(l.mkpartial());
        p3->val() = string("wibble");
        l.pushtail(p3);
        l.pushtail(p2);
        assert(l.pophead() == "f");
        auto p4(l.mkpartial());
        l.pushtail(p4) = string("moo");
        assert(l.pophead() == "wibble");
        assert(l.pophead() == "bar");
        assert(l.pophead() == "moo");
        assert(l.empty());
        delete l.mkpartial("bazz");
        assert(l.empty()); },
    "serialise", [] {
        quickcheck q;
        ::serialise<list<int> >(q); },
    "copy", [] {
        quickcheck q;
        for (unsigned x = 0; x < 100; x++) {
            list<int> y(q);
            list<int> z(y);
            assert(y == z); } },
    "transfer", [] {
        quickcheck q;
        for (unsigned x = 0; x < 100; x++) {
            list<int> y(q);
            list<int> sparey(y);
            list<int> z;
            z.transfer(y);
            assert(z == sparey);
            assert(y.empty()); }
        list<int> x;
        x.pushtail(1,2,3);
        list<int> y;
        y.pushtail(4,5,6);
        x.transfer(y);
        assert(y.empty());
        y.pushtail(1,2,3,4,5,6);
        assert(x == y); },
    "head", [] {
        auto x(list<int>::mk(1,2,3));
        assert(x.idx(0) == 1);
        assert(x.idx(1) == 2);
        assert(x.idx(2) == 3);
        assert(x.peekhead() == 1);
        assert(x.peekhead() == 1);
        x.pushhead(4);
        assert(x.idx(0) == 4);
        assert(x.idx(1) == 1);
        assert(x.idx(2) == 2);
        assert(x.idx(3) == 3);
        const list<int> &y(x);
        assert(y.idx(0) == x.idx(0));
        assert(y.idx(1) == x.idx(1));
        assert(x.peekhead() == 4);
        x.drophead();
        assert(x.peekhead() == 1);
        x.drophead();
        assert(x.peekhead() == 2);
        x.drophead();
        assert(x.peekhead() == 3);
        x.pushhead(5);
        assert(x.peekhead() == 5);
        x.drophead();
        assert(x.peekhead() == 3);
        x.drophead();
        assert(x.empty());
        x.pushhead(99);
        assert(x.peekhead() == 99);
        x.drophead();
        assert(x.empty()); },
    "constiter", [] {
        struct f { int x; f(int y) : x(y) {} };
        auto x(list<f>::mk(1,2,3));
        const list<f> &y(x);
        int cntr = 1;
        auto it(y.start());
        while (!it.finished()) {
            assert(it->x == cntr);
            it.next();
            cntr++; } },
    "dupes", [] {
        list<int> x;
        assert(!x.hasdupes());
        x.pushtail(1);
        assert(!x.hasdupes());
        x.pushtail(2);
        assert(!x.hasdupes());
        x.pushtail(1);
        assert(x.hasdupes()); },
    "drop", [] {
        auto x(list<int>::mk(1,2,5));
        x.drop(1);
        assert(x == list<int>::mk(2,5));
        x.drop(5);
        assert(x == list<int>::mk(2));
        x.drop(2);
        assert(x == list<int>::mk()); },
    "Immediate", [] {
        assert(list<int>::mk() == list<int>(Immediate()));
        assert(list<int>::mk(1,2,3) == list<int>(Immediate(), 1, 2, 3)); },
    "assigniter", [] {
        list<int> l(Immediate(), 1,2,3);
        auto it1(l.start());
        auto it2(l.start());
        auto it3(l.start());
        assert(*it1 == 1);
        it1.next();
        assert(*it1 == 2);
        it1.next();
        assert(*it1 == 3);
        it1.next();
        assert(it1.finished());
        it2 = it1;
        assert(it2.finished());
        it1 = it3;
        assert(*it1 == 1);
        it1.next();
        assert(*it1 == 2);
        it1.next();
        assert(*it1 == 3);
        it1.next();
        assert(it1.finished());
        assert(*it3 == 1);
        it3.next();
        assert(*it3 == 2);
        it3.next();
        assert(*it3 == 3);
        it3.next();
        assert(it3.finished()); },
    "assignreverse", [] {
        list<int> l(Immediate(), 1,2,3);
        auto it1(l.start());
        auto it2(l.reverse());
        assert(*it2 == 3);
        it2.next();
        assert(*it2 == 2);
        it2 = it1;
        assert(*it2 == 1);
        it2.next();
        assert(*it2 == 2);
        it2.next();
        assert(*it2 == 3);
        it2.next();
        assert(it2.finished()); },
    "dropptr", [] {
        list<int> l(Immediate(), 1, 1, 1);
        auto it(l.start());
        auto p1(&*it);
        it.next();
        auto p2(&*it);
        it.next();
        auto p3(&*it);
        l.drop(p2);
        assert(l == list<int>(Immediate(), 1, 1));
        it = l.start();
        assert(&*it == p1);
        it.next();
        assert(&*it == p3);
        l.drop(p3);
        assert(l == list<int>(Immediate(), 1));
        assert(&*l.start() == p1);
        l.drop(p1);
        assert(l == list<int>(Immediate())); },
    "empty_unsafe", [] {
        /* empty_unsafe() is like empty(), except that it does
         * less sanity checking and it's safe while the list is
         * under concurrent modification. */
        list<int> l;
        assert(l.empty_unsafe());
        l.append(1);
        assert(!l.empty_unsafe());
        bool done = false;
        spark<void> checker([&] {
                /* This doesn't need full acquire semantics; it just
                 * needs to not get completely eliminated by the
                 * compiler.  Acquire is easier, though. */
                while (!loadacquire(done)) l.empty_unsafe(); });
        auto deadline((2_s).future());
        while (deadline.infuture()) {
            l.append(5);
            l.pophead(); }
        done = true; },
    "parser", [] {
        auto &p(list<int>::parser(parsers::intparser<int>()));
        assert(p.match("{}") == list<int>());
        assert(p.match("{1}") == list<int>::mk(1));
        assert(p.match("{1 2}") == list<int>::mk(1, 2));
        assert(p.match("{1\t\r\n    3}") == list<int>::mk(1, 3));
        assert(p.match("{") == error::noparse);
        assert(p.match("") == error::noparse);
        assert(p.match("{abc}") == error::noparse);
        auto r((p+parsers::strparser).match("{1 2 3}abc").fatal("dead"));
        assert(r.first() == list<int>::mk(1, 2, 3));
        assert(!strcmp(r.second(), "abc"));
        auto &p2(list<const char *>::parser(parsers::strparser));
        auto r2(p2.match("{abc def hij}").fatal("dood"));
        assert(!strcmp(r2.idx(0), "abc"));
        assert(!strcmp(r2.idx(1), "def"));
        assert(!strcmp(r2.idx(2), "hij")); },
    "parserobj", [] {
        auto &p(list<::withparser>::parser());
        assert(p.match("{}") == list<::withparser>());
        assert(p.match("{foo}") == list<::withparser>(Immediate(), 1));
        assert(p.match("{foo bar}") == list<::withparser>(Immediate(), 1, 2));},
    "field", [] {
        auto &p(list<int>::parser(parsers::intparser<int>()));
        quickcheck q;
        for (unsigned x = 0; x < 100; x++) {
            list<int> l(q);
            auto b(fields::mk(l).c_str());
            assert(p.match(b) == l); } },
    "crashfield", [] {
        class cc {
        public: const fields::field &field(crashcontext) const {
            return fields::mk("X"); } };
        list<cc> l;
        l.append();
        l.append();
        l.append();
        assert(!strcmp(l.field(*(crashcontext *)NULL).c_str(),
                       "{X X X}")); },
    "->", [] {
        struct foo {
            int x;
            int y;
            foo(int _x, int _y) : x(_x), y(_y) {} };
        list<foo> bar;
        bar.append(1, 2);
        bar.append(3, 4);
        auto it(bar.start());
        assert(it->x == 1);
        assert(it->y == 2);
        it.next();
        assert(it->x == 3);
        assert(it->y == 4);
        it.next();
        assert(it.finished()); },
    "issorted", [] {
        assert(list<int>(Immediate()).issorted());
        assert(list<int>(Immediate(), 5).issorted());
        assert(list<int>(Immediate(), 5, 6).issorted());
        assert(list<int>(Immediate(), 5, 6, 7).issorted());
        assert(!list<int>(Immediate(), 6, 5).issorted());
        assert(!list<int>(Immediate(), 5, 6, -100).issorted());
        /* Check that issorted() and sort() agree on the desired order. */
        list<int> l(Immediate(), 5, 7, 1, 99, -5);
        assert(!l.issorted());
        sort(l);
        assert(l.issorted()); },
    "steal", [] {
        class cons {
        public: int &_nrcons;
        public: cons(int &__nrcons) : _nrcons(__nrcons) { _nrcons++; } };
        int nrcons(0);
        list<cons> l((Immediate()), cons(nrcons), cons(nrcons), cons(nrcons));
        assert(nrcons == 3);
        list<cons> l2(Steal, l);
        assert(nrcons == 3); },
    "peektail", [] {
        list<int> l(Immediate(), 1, 2, 3);
        assert(l.peektail() == 3);
        assert(l.peektail() == 3);
        assert(l.poptail() == 3);
        assert(l.peektail() == 2); },
    "droptail", [] {
        list<int> l(Immediate(), 1, 2, 3);
        assert(l == list<int>(Immediate(), 1, 2, 3));
        l.droptail();
        assert(l == list<int>(Immediate(), 1, 2));
        l.droptail();
        assert(l == list<int>(Immediate(), 1));
        l.droptail();
        assert(l == list<int>(Immediate())); } );
