#include "streamname.H"

#include "fields.H"
#include "parsers.H"

#include "parsers.tmpl"
#include "wireproto.tmpl"

/* Not quite a simple wrapper, because a string can encode values
   (e.g. '/', anything non-printable) which aren't allowed in a
   streamname, and so we can't use the simple wrapper macros. */
namespace wireproto {
template <> maybe<streamname>
deserialise<streamname>(wireproto::bufslice &slice) {
    auto r(deserialise<string>(slice));
    if (r == Nothing) return Nothing;
    else return streamname::mk(r.just()); }
template <> tx_message& tx_message::addparam<streamname>(
    parameter<streamname> param,
    streamname const &val) {
    return addparam(parameter<string>(param), val.content); } }

const fields::field &
fields::mk(const streamname &s) {
    return "<stream:" + mk(s.content) + ">"; }

streamname::streamname(const string &o)
    : content(o) {}

maybe<streamname>
streamname::mk(const string &s) {
    auto l(s.len());
    if (l == 0) return Nothing;
    auto c(s.c_str());
    for (unsigned x = 0; x < l; x++) {
        if (!isprint(c[x]) || c[x] == '/') return Nothing; }
    return streamname(s); }

string
streamname::asfilename() const {
    return content; }

const parser<streamname> &
parsers::_streamname() {
    return ("<stream:" + strparser + ">")
        .map<streamname>([] (const char *x) {
                return streamname(x); }); }
