#include "clustername.H"
#include "serialise.H"
#include "test2.H"

#include "fields.tmpl"
#include "parsers.tmpl"
#include "serialise.tmpl"
#include "test2.tmpl"

static testmodule __testclustername(
    "clustername",
    list<filename>::mk("clustername.C", "clustername.H"),
    testmodule::LineCoverage(100_pc),
    testmodule::BranchCoverage(80_pc),
    "parsers", [] { parsers::roundtrip(parsers::__clustername()); },
    "serialise", [] {
        quickcheck q;
        serialise<clustername>(q); },
    "==", [] {
        assert(clustername::mk("a") == clustername::mk("a"));
        assert(clustername::mk("a") != clustername::mk("b")); },
    "badparse", [] {
        assert(parsers::__clustername()
               .match(string("<gjdkgdjl>")) == error::noparse);
        string s = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        while (s.len() < clustername::maxsize) s = s + s;
        assert(parsers::__clustername()
               .match(string("<clustername:") + s + string(">"))
               == error::overflowed); },
    "baddeserialise", [] {
        string s = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        while (s.len() < clustername::maxsize) s = s + s;
        ::buffer b;
        serialise1(b).push(s);
        deserialise1 ds(b);
        clustername cn(ds);
        assert(ds.isfailure()); });
