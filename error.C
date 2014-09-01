#include "error.H"

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "fields.H"
#include "logging.H"
#include "parsers.H"
#include "string.H"
#include "test.H"

#include "list.tmpl"
#include "parsers.tmpl"
#include "test.tmpl"
#include "wireproto.tmpl"

static const int firsterror = 0;
const error error::unknown(0);
const error error::disconnected(-1);
const error error::overflowed(-2);
const error error::underflowed(-3);
const error error::missingparameter(-4);
const error error::invalidmessage(-5);
const error error::unrecognisedmessage(-6);
const error error::noparse(-7);
const error error::timeout(-8);
const error error::truncated(-9);
const error error::unimplemented(-10);
const error error::badversion(-11);
const error error::authenticationfailed(-12);
const error error::ratelimit(-13);
const error error::invalidparameter(-14);
const error error::already(-15);
const error error::notfound(-16);
const error error::notafile(-17);
const error error::toolate(-18);
const error error::toosoon(-19);
const error error::pastend(-20);
const error error::nothing(-21);
const error error::notempty(-22);
const error error::wouldblock(-23);
static const int lasterror = 23;

error::error(const quickcheck &q) {
    if ((bool)q) e = (unsigned)q % 300 + 1;
    else e = firsterror - ((unsigned)q % (lasterror+1)); }

class errorfield : public fields::field {
    error content;
    errorfield(const error &_content)
        : content(_content)
        {}
public:
    static const field &n(const error &content)
        { return *new errorfield(content); }
    void fmt(fields::fieldbuf &buf) const
        {
            if (content.e > 0) {
                buf.push(strerror(content.e));
            } else if (content == error::unknown) {
                buf.push("unknown");
            } else if (content == error::disconnected) {
                buf.push("disconnected");
            } else if (content == error::overflowed) {
                buf.push("overflowed");
            } else if (content == error::underflowed) {
                buf.push("underflowed");
            } else if (content == error::missingparameter) {
                buf.push("missingparameter");
            } else if (content == error::invalidmessage) {
                buf.push("invalidmessage");
            } else if (content == error::unrecognisedmessage) {
                buf.push("unrecognisedmessage");
            } else if (content == error::noparse) {
                buf.push("noparse");
            } else if (content == error::timeout) {
                buf.push("timeout");
            } else if (content == error::truncated) {
                buf.push("truncated");
            } else if (content == error::unimplemented) {
                buf.push("unimplemented");
            } else if (content == error::badversion) {
                buf.push("badversion");
            } else if (content == error::authenticationfailed) {
                buf.push("authenticationfailed");
            } else if (content == error::ratelimit) {
                buf.push("ratelimit");
            } else if (content == error::invalidparameter) {
                buf.push("invalidparameter");
            } else if (content == error::already) {
                buf.push("already");
            } else if (content == error::notfound) {
                buf.push("notfound");
            } else if (content == error::notafile) {
                buf.push("notafile");
            } else if (content == error::toolate) {
                buf.push("toolate");
            } else if (content == error::toosoon) {
                buf.push("toosoon");
            } else if (content == error::pastend) {
                buf.push("pastend");
            } else if (content == error::nothing) {
                buf.push("nothing");
            } else if (content == error::notempty) {
                buf.push("notempty");
            } else if (content == error::wouldblock) {
                buf.push("wouldblock");
            } else {
                ("<invalid error " + fields::mk(content.e) + ">")
                    .fmt(buf); } } };

const fields::field &
fields::mk(const error &e)
{
    return errorfield::n(e);
}

error
error::from_errno()
{
    int e = errno;
    errno = 99; /* deliberately clobber errno to catch people using it
                   when they should be using a returned error code. */
    return error(e);
}

error
error::from_errno(int err)
{
    return error(err);
}

/* Can't really test these from the unit tests */
#ifndef COVERAGESKIP
void
error::fatal(const fields::field &msg) const
{
    logmsg(loglevel::emergency,
           "fatal error: " + msg + ": " + fields::mk(*this));
    fields::fieldbuf buf;
    (msg + ": " + fields::mk(*this)).fmt(buf);
    errx(1, "fatal error: %s", buf.c_str());
}

void
error::fatal(const char *msg) const
{
    fatal(fields::mk(msg));
}
#endif

void
error::warn(const fields::field &msg) const
{
    logmsg(loglevel::failure,
           "warning: " + msg + ": " + fields::mk(*this));
    fields::fieldbuf buf;
    (msg + ": " + fields::mk(*this)).fmt(buf);
    warnx("warning: %s", buf.c_str());
}

void
error::warn(const char *msg) const
{
    warn(fields::mk(msg));
}

wireproto_simple_wrapper_type(error, int, e);

void
tests::_error() {
    testcaseV("error", "eq", [] {
            for (int x = -lasterror;
                 x <= firsterror + 10;
                 x++) {
                for (int y = -lasterror;
                     y <= firsterror + 10;
                     y++) {
                    assert((::error(x) == ::error(y)) ==
                           (x == y)); } } });
    testcaseV("error", "neq", [] {
            for (int x = -lasterror;
                 x <= firsterror + 10;
                 x++) {
                for (int y = -lasterror;
                     y <= firsterror + 10;
                     y++) {
                    assert((::error(x) != ::error(y)) ==
                           (x != y)); } } });
    testcaseV("error", "uniqfields", [] {
            list<string> fmted;
            for (int x = -lasterror; x <= firsterror + 10; x++) {
                fmted.pushtail(string(fields::mk(error(x)).c_str())); }
            assert(!fmted.hasdupes());
            fmted.flush(); });
    testcaseV("error", "errno", [] {
            errno = 7;
            auto e(error::from_errno());
            assert(errno == 99);
            assert(e == error::from_errno(7)); });
    testcaseV("error", "wireproto", [] {
            wireproto::roundtrip<error>(); });
    testcaseV("error", "fmtinvalid", [] {
            assert(!strcmp(fields::mk(error(-99)).c_str(),
                           "<invalid error -99>")); });
#if TESTING
    testcaseV("error", "warn", [] {
            bool warned = false;
            eventwaiter<loglevel> logwait(
                tests::logmsg,
                [&warned] (loglevel level) {
                    if (level == loglevel::failure) {
                        assert(!warned);
                        warned = true; } });
            error::underflowed.warn("should underflow"); });
#endif
}
