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
const actortype
actortype::cli(3);
const actortype
actortype::test(4);

const fields::field &
fields::mk(actortype t) {
    if (t == actortype::master) return fields::mk("master");
    else if (t == actortype::storageslave) return fields::mk("storage");
    else if (t == actortype::cli) return fields::mk("cli");
    else if (t == actortype::test) return fields::mk("test");
    else abort(); }

const parser<actortype> &
parsers::_actortype() {
    return strmatcher("master", actortype::master) ||
        strmatcher("storage", actortype::storageslave) ||
        strmatcher("cli", actortype::cli) ||
        strmatcher("test", actortype::test); }

actortype::actortype(quickcheck q) {
    switch ((unsigned)q % 4) {
    case 0: val = actortype::master.val; break;
    case 1: val = actortype::storageslave.val; break;
    case 2: val = actortype::cli.val; break;
    case 3: val = actortype::test.val; break; } }

wireproto_simple_wrapper_type(actortype, int, val);

void
tests::_actortype() {
    testcaseV("actortype", "parsers", [] {
            parsers::roundtrip(parsers::_actortype()); });
    testcaseV("actortype", "wireproto", [] {
            wireproto::roundtrip<actortype>(); }); }
