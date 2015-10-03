#include "serialise.H"
#include "test2.H"
#include "void.H"

#include "orerror.tmpl"
#include "parsers.tmpl"
#include "test2.tmpl"

static testmodule __testvoid(
    "void",
    list<filename>::mk("void.C", "void.H"),
    /* There are no branches in void.C, but gcov counts some static
     * initilisation stuff anyway. */
    testmodule::BranchCoverage(50_pc),
    "field", [] { assert(!strcmp(Void().field().c_str(), "")); },
    "==", [] { assert(Void() == Void()); },
    "serialise", [] {
        Void v;
        buffer buf;
        {   serialise1 s(buf);
            v.serialise(s); }
        {   deserialise1 ds(buf);
            Void vv(ds);
            assert(ds.status().issuccess());
            assert(vv == v); } },
    "parser", [] {
        assert(Void::parser().match("") == Void());
        assert(Void::parser().match("whatever") == error::noparse); } );
