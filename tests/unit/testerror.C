#include "error.H"
#include "logging.H"
#include "serialise.H"
#include "test.H"
#include "test2.H"

#include "serialise.tmpl"
#include "test.tmpl"
#include "test2.tmpl"

using namespace __error_private;

static testmodule __testerror(
    "error",
    list<filename>::mk("error.C", "error.H"),
    testmodule::BranchCoverage(95_pc),
    testmodule::LineCoverage(90_pc),
    "eq", [] {
        for (int x = -lasterror;
             x <= firsterror + 10;
             x++) {
            for (int y = -lasterror;
                 y <= firsterror + 10;
                 y++) {
                assert((error::__test_mkerr(x) == error::__test_mkerr(y)) ==
                       (x == y)); } } },
    "neq", [] {
        for (int x = -lasterror;
             x <= firsterror + 10;
             x++) {
            for (int y = -lasterror;
                 y <= firsterror + 10;
                 y++) {
                assert((error::__test_mkerr(x) != error::__test_mkerr(y)) ==
                       (x != y)); } } },
    "uniqfields", [] {
        list<string> fmted;
        for (int x = -lasterror; x <= firsterror + 10; x++) {
            fmted.pushtail(string(fields::mk(error::__test_mkerr(x)).c_str()));}
        assert(!fmted.hasdupes()); },
    "errno", [] {
        errno = 7;
        auto e(error::from_errno());
        assert(errno == 99);
        assert(e == error::from_errno(7)); },
    "fmtinvalid", [] {
        assert(!strcmp(fields::mk(error::__test_mkerr(-99)).c_str(),
                       "<invalid error -99>")); },
    "serialise", [] {
        quickcheck q;
        serialise<error>(q); }
#if TESTING
    , "warn", [] {
        bool warned = false;
        tests::eventwaiter<loglevel> logwait(
            tests::logmsg,
            [&warned] (loglevel level) {
                if (level == loglevel::failure) {
                    assert(!warned);
                    warned = true; } });
        error::underflowed.warn("should underflow"); }
#endif
    );
