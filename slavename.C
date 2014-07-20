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

const parser< ::slavename> &
parsers::slavename() {
    return ("<slavename:" + strparser + ">").map< ::slavename>(
        [] (const char *const&what) {
            return ::slavename(what); }); }
