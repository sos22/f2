#include "enumerate.H"
#include "logging.H"
#include "test2.H"

#include "enumerate.tmpl"
#include "test2.tmpl"

template <int a, int b> class r {
public: int i;
public: r(enumerator &e) : i(e.range<int>(a,b)) {}
public: r(int _i) : i(_i) {}
public: bool operator==(r o) const { return o.i == i; }
public: const fields::field &field() const { return fields::mk(i); } };

static testmodule __testenumerate(
    "enumerate",
    list<filename>::mk("enumerate.C",
                       "enumerate.H",
                       "enumerate.tmpl"),
    "range", [] {
        assert((enumerate<r<0,0> >() == list<r<0,0> >::mk(0)));
        assert((enumerate<r<0,1> >() == list<r<0,1> >::mk(0,1)));
        assert((enumerate<r<-5,5> >() ==
                list<r<-5, 5> >::mk(-5,-4,-3,-2,-1,0,1,2,3,4,5))); },
    "nested", [] {
        class inner1 {
        public: int i;
        public: inner1(enumerator &e) : i(e.range(0,2)) {}
        public: inner1(int _i) : i(_i) {}
        public: const fields::field &field() const { return fields::mk(i); }
        public: bool operator==(inner1 o) const { return i == o.i; } };
        class inner2 {
        public: int i;
        public: inner2(enumerator &e) : i(e.range(0,1)) {}
        public: inner2(int _i) : i(_i) {}
        public: const fields::field &field() const { return fields::mk(i); }
        public: bool operator==(inner2 o) const { return i == o.i; } };
        class outer {
        public: inner1 a;
        public: inner2 b;
        public: outer(enumerator &e) : a(e), b(e) {}
        public: outer(inner1 _a, inner2 _b) : a(_a), b(_b) {}
        public: const fields::field &field() const {
            return "<" + a.field() + ":" + b.field() + ">"; }
        public: bool operator==(outer o) const {
            return a == o.a && b == o.b; } };
        assert(enumerate<outer>() == list<outer>::mk(
                   outer(0, 0),
                   outer(1, 0),
                   outer(2, 0),
                   outer(0, 1),
                   outer(1, 1),
                   outer(2, 1))); });
