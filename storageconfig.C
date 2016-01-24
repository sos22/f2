#include "storageconfig.H"

#include "parsers.H"

#include "parsers.tmpl"

storageconfig::storageconfig(const filename &f, const beaconserverconfig &c)
    : poolpath(f), beacon(c) {}

storageconfig::storageconfig(deserialise1 &ds)
    : poolpath(ds), beacon(ds) {}

void
storageconfig::serialise(serialise1 &s) const {
    s.push(poolpath);
    s.push(beacon); }

bool
storageconfig::operator==(const storageconfig &o) const {
    return poolpath == o.poolpath && beacon == o.beacon; }

const fields::field &
storageconfig::field() const {
    return
        "<storageconfig:"
        " poolpath:" + poolpath.field() +
        " beacon:" + beacon.field() +
        ">"; }

const parser<storageconfig> &
storageconfig::parser() {
    auto &i("<storageconfig:" +
            ~(" poolpath:" + filename::parser()) +
            " beacon:" + beaconserverconfig::parser() +
            ">");
    class f : public ::parser<storageconfig> {
    public: decltype(i) inner;
    public: f(decltype(i) ii) : inner(ii) {}
    public: orerror<result> parse(const char *what) const {
        auto i(inner.parse(what));
        if (i.isfailure()) return i.failure();
        else return i.success().map<storageconfig>([] (auto x) {
                return storageconfig(
                    x.first().dflt(filename("storagepool")),
                    x.second()); }); } };
    return *new f(i); }
