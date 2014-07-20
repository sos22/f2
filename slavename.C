#include "slavename.H"

#include "fields.H"
#include "parsers.H"

#include "parsers.tmpl"
#include "wireproto.tmpl"

wireproto_simple_wrapper_type(slavename, string, content)

slavename::slavename(const slavename &o)
    : content(o.content) {}

const fields::field &
fields::mk(const slavename &s) {
    return "<slavename:" + mk(s.content).escape() + ">"; }

class slavenameparser_ : public parser<slavename> {
private: orerror<result> parse(const char *what) const {
    return ("<slavename:" + strparser + ">")
        .map<slavename>([] (const char *s) { return slavename(s); })
        .parse(what); }
};
static slavenameparser_ slavenameparser;
parser< ::slavename> &parsers::slavename(slavenameparser);
template class parser< ::slavename>;
