#include "streamname.H"

#include "either.H"
#include "fields.H"
#include "parsers.H"
#include "serialise.H"

#include "parsers.tmpl"

const fields::field &
fields::mk(const streamname &s) {
    return "<stream:" + mk(s.content) + ">"; }

streamname::streamname(const string &o)
    : content(o) {}

maybe<streamname>
streamname::mk(const string &s) {
    streamname res(s);
    if (!res.isvalid()) return Nothing;
    else return res; }

bool
streamname::isvalid() const {
    auto l(content.len());
    if (l == 0) return false;
    auto c(content.c_str());
    for (unsigned x = 0; x < l; x++) {
        if (!isprint(c[x]) || c[x] == '/') return false; }
    return true; }

string
streamname::asfilename() const {
    return content; }

bool
streamname::operator<(const streamname &o) const {
    return content < o.content; }

bool
streamname::operator>(const streamname &o) const {
    return content > o.content; }

streamname::streamname(deserialise1 &ds)
    : content(ds) {
    if (!isvalid()) {
        ds.fail(error::invalidmessage);
        content = "...badstream..."; } }

void
streamname::serialise(serialise1 &s) const { content.serialise(s); }

const parser<streamname> &
parsers::_streamname() {
    return (("<stream:" + strparser + ">") || strparser)
        .maperr<streamname>(
            [] (const char * x) -> orerror<streamname> {
                auto r(streamname::mk(x));
                if (r == Nothing) return error::noparse;
                else return r.just(); }); }
