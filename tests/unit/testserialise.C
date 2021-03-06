#include "serialise.H"
#include "test2.H"

#include "orerror.tmpl"
#include "serialise.tmpl"
#include "test2.tmpl"

deserialiseT::deserialiseT(buffer &_src) : deserialise1(_src) {}
serialiseT::serialiseT(buffer &_dest) : serialise1(_dest) {}

template <typename t> void
serialisefundamental(quickcheck &q, unsigned nr = 1000) {
    for (unsigned x = 0; x < nr; x++) {
        t val(q);
        buffer buf;
        serialise1 s(buf);
        s.push(val);
        deserialise1 ds(buf);
        assert((t)ds == val);
        assert(!ds.status().isfailure());
        assert(buf.avail() == ds.offset()); } }

class testcompound1 {
public: int field1;
public: long field2;
public: testcompound1(int f1, long f2) : field1(f1), field2(f2) {}
public: testcompound1(deserialise1 &ds) : field1(ds), field2(ds) {}
public: void serialise(serialise1 &ctxt) const {
    ctxt.push(field1);
    ctxt.push(field2); }
public: testcompound1(deserialiseT &ds) {
    field2 = ds;
    field1 = ds; }
public: void serialise(serialiseT &ctxt) const {
    ctxt.push(field2);
    ctxt.push(field1); }
public: bool operator==(const testcompound1 &o) const {
    return field1 == o.field1 && field2 == o.field2; } };

static testmodule __testserialise(
    "serialise",
    list<filename>::mk("serialise.C", "serialise.H", "serialise.tmpl"),
    testmodule::LineCoverage(80_pc),
    testmodule::BranchCoverage(55_pc),
    "genrandom", [] {
        quickcheck q;
        deserialise1 ds(q);
        list<long> results;
        for (unsigned x = 0; x < 10000; x++) results.append(ds);
        sort(results);
        /* Should be a decent range and a decent number of unique
         * values. */
        assert(results.pophead() < -(1l << 33));
        assert(results.poptail() > (1l << 33));
        list<long> uniques;
        auto contains([] (const list<long> &l, long what) -> bool {
                for (auto it(l.start()); !it.finished(); it.next()) {
                    if (*it == what) return true; }
                return false; });
        for (auto it(results.start()); !it.finished(); it.next()) {
            if (!contains(uniques, *it)) uniques.pushtail(*it); }
        assert(uniques.length() > results.length() / 10);
        /* A couple of special values must always be present. */
        assert(contains(uniques, 0));
        assert(contains(uniques, 1));
        assert(contains(uniques, -1));
        assert(contains(uniques, 2));
        assert(contains(uniques, 10)); },
    "fundamentals", [] {
        quickcheck q;
        serialisefundamental<long>(q);
        serialisefundamental<unsigned long>(q);
        serialisefundamental<int>(q);
        serialisefundamental<unsigned int>(q);
        serialisefundamental<short>(q);
        serialisefundamental<unsigned short>(q);
        serialisefundamental<char>(q);
        serialisefundamental<unsigned char>(q);
        serialisefundamental<bool>(q); },
    "bad", [] {
        {   ::buffer b;
            deserialise1 ds(b);
            assert((bool)ds == false);
            assert(ds.status() == error::underflowed); }
        {   ::buffer b;
            b.queue("Z", 1);
            deserialise1 ds(b);
            assert((bool)ds == false);
            assert(ds.status() == error::invalidmessage); }
        {   ::buffer b;
            b.queue("Z", 1);
            deserialise1 ds(b);
            assert((unsigned long)ds == 0);
            assert(ds.status() == error::underflowed); } },
    "compound", [] {
        quickcheck q;
        serialise<testcompound1>(q, 1000); },
    "upgrade", [] {
        for (unsigned x = 0; x < 1000; x++) {
            quickcheck q;
            auto val(mkrandom<testcompound1>(q));
            ::buffer b;
            if (random() % 2) {
                serialise1 s(b);
                version::current.serialise(s);
                val.serialise(s); }
            else {
                serialiseT s(b);
                version::invalid.serialise(s);
                val.serialise(s); }
            deserialise1 ds(b);
            if ((version)ds == version::current) {
                assert(testcompound1(ds) == val);
                assert(!ds.isfailure());
                assert(b.avail() == ds.offset()); }
            else {
                deserialiseT dsT(b);
                assert((version)dsT == version::invalid);
                assert(testcompound1(dsT) == val);
                assert(!dsT.isfailure());
                assert(b.avail() == dsT.offset()); } } });
