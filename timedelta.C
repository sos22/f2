#include "timedelta.H"

#include "parsers.H"
#include "quickcheck.H"
#include "fields.H"
#include "serialise.H"
#include "timestamp.H"

#include "parsers.tmpl"

timedelta::timedelta(const quickcheck &q, timedelta min, timedelta max)
    : v(min == max
        ? min.v
        : min.v + ((unsigned long)q % (max.v - min.v))) {}

timedelta::timedelta(const quickcheck &q) : v(q) {}

timedelta::timedelta(deserialise1 &ds) : v(ds) {}

timestamp
timedelta::operator+(timestamp ts) const { return ts + *this; }

const fields::field &
timedelta::field() const { return "<timedelta:" + fields::mk(v) + "ns>"; }

const ::parser<timedelta> &
timedelta::parser() {
    class p : public ::parser< ::timedelta> {
    public: const parser<long> &inner;
    public: p() : inner(parsers::intparser<long>()) {}
    public: orerror<result> parse(const char *what) const {
        auto r(inner.parse(what));
        if (r.isfailure()) return r.failure();
        else {
            return result(r.success().left,
                          timedelta::nanoseconds(r.success().res)); } } };
    return ("<timedelta:" + *(new p()) + "ns>"); }

timestamp
timedelta::future() const { return timestamp::now() + *this; }

timedelta
timedelta::time(std::function<void ()> what) {
    auto start(timestamp::now());
    what();
    return timestamp::now() - start; }

timespec
timedelta::astimespec() const {
    timespec res;
    res.tv_sec = v / 1000000000;
    res.tv_nsec = v % 1000000000;
    if (res.tv_nsec < 0) {
        res.tv_sec--;
        res.tv_nsec += 1000000000; }
    return res; }

void
timedelta::serialise(serialise1 &s) const { s.push(v); }
