#include "maybe.H"

#include "parsers.H"

#include "parsers.tmpl"

maybe<void>
maybe<void>::just;

const ::parser<maybe<void> > &
maybe<void>::parser(const ::parser<void> &i) {
    class f : public ::parser<Void> {
    public: const parser<void> &_i;
    public: f(const parser<void> &__i) : _i(__i) {}
    public: orerror<result> parse(const char *what) const {
        return _i.parse(what).map<result>([] (auto r) {
                return result(r, Void()); }); } };
    class g : public ::parser<maybe<void> > {
    public: const parser<maybe<Void> > &i;
    public: g(const parser<maybe<Void> > &_i) : i(_i) {}
    public: orerror<result> parse(const char *what) const {
        return i.parse(what).map<result>([] (auto r) {
                return r.map<maybe<void> >([] (auto rr) {
                        if (rr.isnothing()) return maybe<void>(Nothing);
                        else return maybe<void>::just; }); }); } };
    return *new g(maybe<Void>::parser(*new f(i))); }
