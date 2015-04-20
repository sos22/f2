#include "clustername.H"
#include "serialise.H"
#include "test2.H"

#include "parsers.tmpl"
#include "serialise.tmpl"
#include "test2.tmpl"

static testmodule __testclustername(
    "clustername",
    list<filename>::mk("clustername.C", "clustername.H"),
    testmodule::LineCoverage(100_pc),
    testmodule::BranchCoverage(70_pc),
    "parsers", [] { parsers::roundtrip(parsers::__clustername()); },
    "serialise", [] {
        quickcheck q;
        serialise<clustername>(q); },
    "baddeserialise", [] {
        string s = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        while (s.len() < clustername::maxsize) s = s + s;
        ::buffer b;
        serialise1(b).push(s);
        deserialise1 ds(b);
        clustername cn(ds);
        assert(ds.isfailure()); });
