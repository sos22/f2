#include "error.H"

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "fields.H"
#include "list.H"
#include "logging.H"
#include "parsers.H"
#include "pubsub.H"
#include "quickcheck.H"
#include "string.H"

#include "list.tmpl"
#include "maybe.tmpl"
#include "parsers.tmpl"
#include "serialise.tmpl"

using namespace __error_private;

const int __error_private::firsterror = 0;
const error error::unknown(0);
const error error::disconnected(-1);
const error error::overflowed(-2);
const error error::underflowed(-3);
const error error::invalidmessage(-4);
const error error::unrecognisedmessage(-5);
const error error::noparse(-6);
const error error::timeout(-7);
const error error::truncated(-8);
const error error::badversion(-9);
const error error::invalidparameter(-10);
const error error::already(-11);
const error error::notfound(-12);
const error error::notafile(-13);
const error error::toolate(-14);
const error error::toosoon(-15);
const error error::pastend(-16);
const error error::nothing(-17);
const error error::notempty(-18);
const error error::wouldblock(-19);
const error error::shutdown(-20);
const error error::range(-21);
const error error::badinterface(-22);
const error error::aborted(-23);
const error error::badqueue(-24);
const error error::eventsdropped(-25);
const error error::badsubscription(-26);
const error error::notadir(-27);
const error error::eqstatemismatch(-28);
const error error::nostorageagents(-29);
const error error::signalled(-30);
const error error::dlopen(-31);
const error error::duplicate(-32);
const error error::sqlite(-33);
/* When adding a new error, make sure you update lasterror and
 * errorfield::fmt() */
const int __error_private::lasterror = 33;

class errorfield : public fields::field {
    error content;
    errorfield(error _content) : content(_content) {}
public:
    static const field &n(error content) {
        return *new errorfield(content); }
    void fmt(fields::fieldbuf &buf) const {
        buf.push("<");
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
        } else if (content == error::badversion) {
            buf.push("badversion");
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
        } else if (content == error::shutdown) {
            buf.push("shutdown");
        } else if (content == error::range) {
            buf.push("range");
        } else if (content == error::badinterface) {
            buf.push("badinterface");
        } else if (content == error::aborted) {
            buf.push("aborted");
        } else if (content == error::badqueue) {
            buf.push("badqueue");
        } else if (content == error::eventsdropped) {
            buf.push("eventsdroped");
        } else if (content == error::badsubscription) {
            buf.push("badsubscription");
        } else if (content == error::notadir) {
            buf.push("notadir");
        } else if (content == error::eqstatemismatch) {
            buf.push("eqstatemismatch");
        } else if (content == error::nostorageagents) {
            buf.push("nostorageagents");
        } else if (content == error::signalled) {
            buf.push("signalled");
        } else if (content == error::signalled) {
            buf.push("signalled");
        } else if (content == error::dlopen) {
            buf.push("dlopen");
        } else if (content == error::duplicate) {
            buf.push("duplicate");
        } else if (content == error::sqlite) {
            buf.push("sqlite");
        } else {
            ("invalid error " + fields::mk(content.e))
                .fmt(buf); }
        buf.push(">"); } };

const fields::field &
fields::mk(error e)
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
}

void
error::warn(const char *msg) const
{
    warn(fields::mk(msg));
}

template void maybe<error>::serialise(serialise1 &) const;
template maybe<error>::maybe(deserialise1 &);

void
error::serialise(serialise1 &s) const { s.push(e); }

error::error(deserialise1 &ds) {
    int _e(ds);
    if ((_e < -lasterror) || _e >= 4096) {
        _e = error::unknown.e;
        ds.fail(error::invalidmessage); }
    e = _e; }

class errorparser : public parser<error> {
public: orerror<result> parse(const char *) const; };
orerror<errorparser::result>
errorparser::parse(const char *what) const {
    /* XXX this is really dumb */
    for (int i = -__error_private::lasterror; i <= 4096; i++) {
        error err(i);
        const char *s = err.field().c_str();
        size_t ss(strlen(s));
        if (!strncmp(what, s, ss)) return result(what + ss, err); }
    return error::noparse; }

const ::parser<error> &
error::parser(void) { return *new errorparser(); }
