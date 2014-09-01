#include "actortype.H"

#include "fields.H"
#include "parsers.H"
#include "test.H"

#include "parsers.tmpl"
#include "wireproto.tmpl"

const actortype
actortype::master(1);
const actortype
actortype::storageslave(2);

const fields::field &
fields::mk(actortype t) {
    if (t == actortype::master) return fields::mk("master");
    else if (t == actortype::storageslave) return fields::mk("storage");
    else abort(); }

const parser<actortype> &
parsers::_actortype() {
    return strmatcher("master", actortype::master) ||
        strmatcher("storage", actortype::storageslave); }

actortype::actortype(quickcheck q)
    : val((bool)q ? master.val : storageslave.val) {}

wireproto_simple_wrapper_type(actortype, int, val);

void
tests::_actortype() {
    testcaseV("actortype", "parsers", [] {
            parsers::roundtrip(parsers::_actortype()); });
    testcaseV("actortype", "wireproto", [] {
            wireproto::roundtrip<actortype>(); }); }
