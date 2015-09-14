#include "orerror.H"
#include "test2.H"

#include "orerror.tmpl"
#include "test2.tmpl"

static testmodule __testorerror(
    "orerror",
    list<filename>::mk("orerror.H", "orerror.tmpl"),
    /* A test with lousy coverage is still better than no test at all... */
    testmodule::LineCoverage(50_pc),
    testmodule::BranchCoverage(1_pc),
    "constructsucc", [] {
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
        assert(s.success().z == 1.5); },
    "constructfail", [] {
        assert(orerror<int>(Failure, error::dlopen) == error::dlopen); },
    "orerrorerror", [] {
        assert(orerror<error>(Failure, error::dlopen).isfailure());
        assert(!orerror<error>(Failure, error::dlopen).issuccess());
        assert(orerror<error>(Failure, error::dlopen).failure()
               == error::dlopen);
        assert(orerror<error>(Success, error::notempty).issuccess());
        assert(!orerror<error>(Success, error::notempty).isfailure());
        assert(orerror<error>(Success, error::notempty).success()
               == error::notempty); },
    "failno=", [] {
        /* orerror<foo> = error::whatever should work even if foo has
         * no = operator. */
        class noeq {
        public: void operator=(const noeq &) = delete; };
        {   orerror<noeq> base(error::already);
            assert(base == error::already);
            base = error::notempty;
            assert(base == error::notempty); }
        {   orerror<noeq> base(Success);
            assert(base.issuccess());
            base = error::range;
            assert(base == error::range); } },
    "=", [] {
        /* Should be able to convert a failure into a success with
         * operator = */
        orerror<int> x(error::already);
        assert(x == error::already);
        x = 5;
        assert(x == 5);
        x = 7;
        assert(x == 7);
        x = error::notempty;
        assert(x == error::notempty); },
    "==", [] {
        assert(orerror<int>(Success, 5) == orerror<int>(Success, 5));
        assert(orerror<int>(Success, 5) != orerror<int>(Success, 6));
        assert(orerror<int>(Success, 5) != orerror<int>(error::dlopen));
        assert(orerror<int>(error::dlopen) != orerror<int>(Success, 7));
        assert(orerror<int>(error::dlopen) != orerror<int>(error::notempty));
        assert(orerror<int>(error::dlopen) == orerror<int>(error::dlopen)); },
    "mksuccess", [] {
        int nrcons(0);
        int nrdest(0);
        class cons {
        public: int &_nrcons;
        public: int &_nrdest;
        public: cons(int &__nrcons, int &__nrdest)
            : _nrcons(__nrcons),
              _nrdest(__nrdest) {
            _nrcons++; }
        public: cons(cons &_what)
            : _nrcons(_what._nrcons),
              _nrdest(_what._nrdest) {
            _nrcons++; }
        public: ~cons() { _nrdest++; }
        public: cons() = delete;
        public: void operator=(const cons &) = delete; };
        orerror<cons> failed(error::unknown);
        failed.mksuccess(nrcons, nrdest);
        assert(failed.issuccess());
        assert(nrcons == 1);
        failed = error::overflowed;
        assert(failed.isfailure());
        assert(nrdest == 1);
        failed.mksuccess(nrcons, nrdest);
        failed.mksuccess(nrcons, nrdest);
        assert(nrcons == 3);
        assert(nrdest == 2); },
    "flatten", [] {
        assert((orerror<orerror<int> >(error::noparse).flatten()
                == error::noparse));
        assert((orerror<orerror<int> >(Success, error::wouldblock).flatten()
                == error::wouldblock));
        assert((orerror<orerror<int> >(Success, 5).flatten()
                == 5)); });
