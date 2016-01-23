#include "percentage.H"

#include "fields.H"
#include "serialise.H"

#include "parsers.tmpl"

percentage::percentage(deserialise1 &ds) : val(ds) {}

void
percentage::serialise(serialise1 &s) const { s.push(val); }

const fields::field &
percentage::field() const { return fields::mk(val) + "%"; }

const parser<percentage> &
percentage::parser() {
    class f : public ::parser<percentage> {
    public: const ::parser<long double> &inner;
    public: f() : inner(parsers::longdoubleparser + "%") {}
    public: orerror<result> parse(const char *what) const {
        return inner.parse(what).map<result>([] (auto r) {
                return r.map<percentage>([] (auto x) {
                        return percentage(x); }); }); } };
    return *new f(); }
