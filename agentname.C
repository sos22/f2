#include "agentname.H"

#include "fields.H"
#include "parsers.H"
#include "quickcheck.H"
#include "serialise.H"

#include "parsers.tmpl"

agentname::agentname(const agentname &o)
    : content(o.content) {}

agentname::agentname(deserialise1 &ds)
    : content(ds) {}

void
agentname::serialise(serialise1 &s) const {
    content.serialise(s); }

bool
agentname::operator==(const agentname &o) const {
    return !(content != o.content); }

bool
agentname::operator!=(const agentname &o) const {
    return content != o.content; }

agentname::agentname(const quickcheck &q)
    : content(q) {}

const fields::field &
fields::mk(const agentname &s) {
    return "<agentname:" + mk(s.content).escape() + ">"; }

const parser< ::agentname> &
agentname::parser() {
    return ("<agentname:" + parsers::strparser + ">").map<agentname>(
        [] (const char *const&what) {
            return agentname(what); }); }
