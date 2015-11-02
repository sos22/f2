#include "jobname.H"

#include "either.H"
#include "fields.H"
#include "parsers.H"
#include "string.H"

#include "parsers.tmpl"

jobname::jobname(deserialise1 &ds) : d(ds) {}

void
jobname::serialise(serialise1 &s) const { d.serialise(s); }

const fields::field &
jobname::field() const { return "<jobname:" + d.field() + ">"; }

string
jobname::asfilename() const { return d.denseprintable(); }

const parser< ::jobname> &
jobname::parser() {
    class inner : public parser< ::jobname> {
    public: const parser<digest> &d;
    public: inner(const parser<digest> &_d) : d(_d) {}
    public: orerror<result> parse(const char *what) const {
        auto r(d.parse(what));
        if (r.isfailure()) return r.failure();
        else return result(r.success().left, jobname(r.success().res)); } };
    auto &d(digest::parser());
    return *new inner(("<jobname:" + d + ">") || d); }
