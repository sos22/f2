#include "clustername.H"

#include "error.H"
#include "parsers.H"
#include "serialise.H"
#include "test.H"

#include "parsers.tmpl"
#include "wireproto.tmpl"

const unsigned
clustername::maxsize = 100;

wireproto_simple_wrapper_type(clustername, string, value);

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

clustername::clustername(const quickcheck &q) {
    do {
        value = (string)q;
    } while (value.len() > maxsize); }

bool
clustername::operator==(const clustername &o) const { return !(*this != o); }

bool
clustername::operator!=(const clustername &o) const { return value != o.value; }

maybe<clustername>
clustername::mk(const string &o) {
    if (o.len() > maxsize) return Nothing;
    else return clustername(o); }

const fields::field &
fields::mk(const clustername &o) {
    return "<clustername:" + mk(o.value).escape() + ">"; }

const parser<clustername> &
parsers::__clustername() {
    return ("<clustername:" + strparser + ">")
        .maperr<clustername>([] (const string &str) -> orerror<clustername> {
                auto r(clustername::mk(str));
                if (r.isjust()) return r.just();
                else return error::overflowed; }); }

void
tests::__clustername() {
    testcaseV("clustername", "parsers", [] {
            parsers::roundtrip(parsers::__clustername()); }); }
