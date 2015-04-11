#include "orerror.H"

#include "test.H"

void
tests::_orerror(void) {
    testcaseV("orerror", "constructsucc", [] {
            class cons {
            public: const int x;
            public: const char *const y;
            public: double z;
            public: cons(int _x, const char *_y, double _z)
                : x(_x),
                  y(_y),
                  z(_z) {}};
            orerror<cons> s(Success, 5, "foo", 1.5);
            assert(s.issuccess());
            assert(s.success().x == 5);
            assert(!strcmp(s.success().y, "foo"));
            assert(s.success().z == 1.5); });
    testcaseV("orerror", "failno=", [] {
            /* orerror<foo> = error::whatever should work even if foo
             * has no = operator. */
            class noeq {
            public: void operator=(const noeq &) = delete; };
            {   orerror<noeq> base(error::already);
                assert(base == error::already);
                base = error::notempty;
                assert(base == error::notempty); }
            {   orerror<noeq> base(Success);
                assert(base.issuccess());
                base = error::range;
                assert(base == error::range); } }); }
