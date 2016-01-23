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
    if (!strcmp(c, ".") || !strcmp(c, "..")) return false;
    return true; }

maybe<streamname>
streamname::mk(const string &s) {
    streamname res(s);
    if (!res.isvalid()) return Nothing;
    else return res; }

string
streamname::asfilename() const { return content; }

streamname::streamname(deserialise1 &ds)
    : content() {
    if (ds.random()) mkrandom(ds);
    else content = string(ds);
    if (!isvalid()) {
        ds.fail(error::invalidmessage);
        content = "...badstream..."; } }

void
streamname::mkrandom(deserialise1 &ds) {
    assert(ds.random());
    auto l = (unsigned)ds % 100 + 1;
    auto c = (char *)malloc(l+1);
    static const char validchars[] =
        "1234567890qwertyuiopasdfghjklzxcvbnm"
        "QWERTYUIOPASDFGHJKLZXCVBNM[]{};'#:@~,.<>?"
        "!\"$%^&*()-=_+\\| ";
    do {
        for (unsigned x = 0; x < l; x++) {
            c[x] = validchars[(unsigned)ds % (sizeof(validchars) - 1)]; }
        c[l] = '\0'; }
    while (!strcmp(c, ".") || !strcmp(c, ".."));
    content = string::steal(c);
    assert(isvalid()); }

streamname::streamname(_Steal, streamname &s) : content(Steal, s.content) {}

void
streamname::serialise(serialise1 &s) const { content.serialise(s); }

const parser<streamname> &
streamname::parser() {
    class f : public ::parser<streamname> {
    public: const ::parser<string> &inner;
    public: f() : inner("<stream:" + string::parser() + ">") {}
    public: orerror<result> parse(const char *what) const {
        auto r(inner.parse(what));
        if (r.isfailure()) return r.failure();
        auto res(streamname::mk(r.success().res));
        if (res == Nothing) return error::noparse;
        return result(r.success().left, res.just()); } };
    return *new f(); }

const parser<streamname> &
streamname::filenameparser() {
    class p : public parser<streamname> {
    private: orerror<result> parse(const char *what) const {
        orerror<result> res(error::noparse);
        auto r(streamname::mk(string(what)));
        if (r != Nothing) res.mksuccess("", Steal, r.just());
        return res; } };
    return *new p(); }

unsigned long
streamname::hash() const { return content.hash(); }

const fields::field &
streamname::field() const { return "<stream:" + content.field() + ">"; }
