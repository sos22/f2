#include "streamname.H"

#include "fields.H"
#include "parsers.H"
#include "serialise.H"

#include "parsers.tmpl"

streamname::streamname(const string &o) : content(o) {}

bool
streamname::isvalid() const {
    auto l(content.len());
    if (l == 0) return false;
    auto c(content.c_str());
    for (unsigned x = 0; x < l; x++) {
        if (!isprint(c[x]) || c[x] == '/') return false; }
    return true; }

maybe<streamname>
streamname::mk(const string &s) {
    streamname res(s);
    if (!res.isvalid()) return Nothing;
    else return res; }

string
streamname::asfilename() const { return content; }

streamname::streamname(deserialise1 &ds)
    : content(ds) {
    if (isvalid()) return;
    else if (!ds.random()) {
        ds.fail(error::invalidmessage);
        content = "...badstream..."; }
    else {
        auto l = (unsigned)ds % 100 + 1;
        auto c = (char *)malloc(l+1);
        static const char validchars[] =
            "1234567890qwertyuiopasdfghjklzxcvbnm"
            "QWERTYUIOPASDFGHJKLZXCVBNM[]{};'#:@~,.<>?"
            "!\"$%^&*()-=_+\\| ";
        for (unsigned x = 0; x < l; x++) {
            c[x] = validchars[(unsigned)ds % (sizeof(validchars) - 1)]; }
        c[l] = '\0';
        content = string::steal(c);
        assert(isvalid()); } }

void
streamname::serialise(serialise1 &s) const { content.serialise(s); }

const parser<streamname> &
streamname::parser() {
    auto &s(string::parser());
    return (("<stream:" + s + ">") || s)
        .maperr<streamname>(
            [] (const string &x) -> orerror<streamname> {
                auto r(streamname::mk(x));
                if (r == Nothing) return error::noparse;
                else return r.just(); }); }

unsigned long
streamname::hash() const { return content.hash(); }

const fields::field &
streamname::field() const { return "<stream:" + content.field() + ">"; }
