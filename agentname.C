#include "agentname.H"

#include "fields.H"
#include "parsers.H"
#include "quickcheck.H"
#include "serialise.H"

#include "parsers.tmpl"

agentname::agentname(const agentname &o)
    : content(o.content) {}

agentname::agentname(deserialise1 &ds)
    : content(ds) {
    if (content.len() > maxsize) ds.fail(error::overflowed); }

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
    : content(q) {
    while (content.len() > maxsize) content = (const char *)q; }

const fields::field &
fields::mk(const agentname &s) {
    return "<agentname:" + mk(s.content).escape() + ">"; }

const parser< ::agentname> &
agentname::parser() {
    class f : public ::parser< ::agentname> {
    public: const ::parser<const char *> &inner;
    public: f() : inner("<agentname:" + parsers::strparser + ">") {}
    public: orerror<result> parse(const char *what) const {
        return inner.parse(what)
            .map<result>([] (auto r) {
                    return r.map<agentname>([] (auto rr) {
                            return agentname(rr); }); });}};
    return *new f(); }
