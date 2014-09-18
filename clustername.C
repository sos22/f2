#include "clustername.H"

#include "parsers.H"

#include "parsers.tmpl"
#include "wireproto.tmpl"

mktupledef(clustername)

const unsigned
clustername::maxsize = 100;

maybe<clustername>
clustername::mk(const string &o) {
    if (o.len() > maxsize) return Nothing;
    else return clustername(o); }

const parser<clustername> &
parsers::__clustername() {
    return ("<clustername: " + strparser + ">")
        .maperr<clustername>([] (const string &str) -> orerror<clustername> {
                auto r(clustername::mk(str));
                if (r.isjust()) return r.just();
                else return error::overflowed; }); }
