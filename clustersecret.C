/* Note that this also implements clustername by being included into
 * clustername.C with some #define's. */
#include "clustersecret.H"

#include <stdlib.h>
#include <string.h>
#include <functional>

#include "pair.H"
#include "fields.H"
#include "parsers.H"
#include "quickcheck.H"
#include "test.H"
#include "tmpheap.H"

#include "parsers.tmpl"
#include "wireproto.tmpl"

/* Limit length to avoid problems with wire protocol message size
 * limits. */
#define MAX_LEN 1000

wireproto_simple_wrapper_type(clustersecret, string, secret);

clustersecret::clustersecret(const quickcheck &q) {
    string s;
    do { s = string(q); } while (s.len() > MAX_LEN);
    secret = s; }

maybe<clustersecret>
clustersecret::mk(const string &what) {
    if (what.len() > MAX_LEN) return Nothing;
    else return clustersecret(what); }

bool
clustersecret::operator==(const clustersecret &o) const {
    return secret == o.secret; }

#define STRING(x) #x
const fields::field &
fields::mk(const clustersecret &rs) {
    return "<" STRING(clustersecret) ":" +
        fields::mk(rs.secret).escape() + ">"; }

const parser<clustersecret> &
parsers::CONCAT2(_, clustersecret)() {
    return ("<" STRING(clustersecret) ":" + parsers::strparser + ">")
        .maperr<clustersecret>(
            [] (const char *x) -> orerror<clustersecret> {
                auto r(clustersecret::mk(x));
                if (r == Nothing) return error::noparse;
                else return r.just(); }); }

void
tests::CONCAT2(_, clustersecret)() {
    testcaseV(STRING(clustersecret), "wireproto", [] {
            wireproto::roundtrip<clustersecret>(); });
    testcaseV(STRING(clustersecret), "parsers", [] {
            parsers::roundtrip(parsers::_clustersecret()); }); }
