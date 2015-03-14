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
            assert(s.success().z == 1.5); }); }
