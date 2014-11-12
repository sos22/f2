#include "slavename.H"

#include "fields.H"
#include "parsers.H"
#include "quickcheck.H"
#include "serialise.H"

#include "parsers.tmpl"

slavename::slavename(const slavename &o)
    : content(o.content) {}

slavename::slavename(deserialise1 &ds)
    : content(ds) {}

void
slavename::serialise(serialise1 &s) const {
    content.serialise(s); }

bool
slavename::operator==(const slavename &o) const {
    return !(content != o.content); }

bool
slavename::operator!=(const slavename &o) const {
    return content != o.content; }

slavename::slavename(const quickcheck &q)
    : content(q) {}

const fields::field &
fields::mk(const slavename &s) {
    return "<slavename:" + mk(s.content).escape() + ">"; }

const parser< ::slavename> &
parsers::_slavename() {
    return ("<slavename:" + strparser + ">").map< ::slavename>(
        [] (const char *const&what) {
            return ::slavename(what); }); }
