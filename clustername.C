#include "clustername.H"

#include "fields.H"
#include "parsers.H"
#include "serialise.H"

#include "parsers.tmpl"

const unsigned
clustername::maxsize = 100;

clustername::clustername(deserialise1 &ds)
    : value(ds) {
    if (value.len() > maxsize) {
        if (ds.random()) value.truncate(value.len() % (maxsize + 1));
        else {
            value = "<cluster name too long>";
            ds.fail(error::overflowed); } } }

void
clustername::serialise(serialise1 &s) const {
    value.serialise(s); }

bool
clustername::operator==(const clustername &o) const { return value == o.value; }

maybe<clustername>
clustername::mk(const string &o) {
    if (o.len() > maxsize) return Nothing;
    else return clustername(o); }

const fields::field &
clustername::field() const {
    return "<clustername:" + value.field().escape() + ">"; }

const ::parser<clustername> &
clustername::parser() {
    class inner : public parser<clustername> {
    public: orerror<result> parse(const char *what) const {
        auto r(("<clustername:" + parsers::strparser + ">").parse(what));
        if (r.isfailure()) return r.failure();
        auto rr(clustername::mk(r.success().res));
        if (rr.isjust()) return result(r.success().left, rr.just());
        else return error::overflowed; } };
    return *new inner(); }
