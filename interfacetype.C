#include "interfacetype.H"

#include "error.H"
#include "fields.H"
#include "parsers.H"
#include "quickcheck.H"
#include "serialise.H"

#include "parsers.tmpl"
#include "serialise.tmpl"

const interfacetype
interfacetype::test(1);
const interfacetype
interfacetype::test2(2);
const interfacetype
interfacetype::meta(4);
const interfacetype
interfacetype::storage(5);
const interfacetype
interfacetype::eq(6);
const interfacetype
interfacetype::coordinator(7);
const interfacetype
interfacetype::compute(8);
const interfacetype
interfacetype::filesystem(9);

interfacetype::interfacetype(unsigned _v)
    : v(_v) {}

interfacetype::interfacetype(quickcheck &q) {
    switch ((unsigned)q % 8) {
    case 0: *this = meta; break;
    case 1: *this = storage; break;
    case 2: *this = eq; break;
    case 3: *this = test; break;
    case 4: *this = test2; break;
    case 5: *this = coordinator; break;
    case 6: *this = compute; break;
    case 7: *this = filesystem; break; } }

interfacetype::interfacetype(deserialise1 &ds)
    : v(ds) {
    if (*this != test &&
        *this != test2 &&
        *this != meta &&
        *this != storage &&
        *this != eq &&
        *this != coordinator &&
        *this != compute &&
        *this != filesystem) {
        ds.fail(error::invalidmessage);
        *this = meta; } }

void
interfacetype::serialise(serialise1 &s) const {
    s.push(v); }

const fields::field &
interfacetype::field() const {
    if (*this == meta) return fields::mk("meta");
    else if (*this == storage) return fields::mk("storage");
    else if (*this == eq) return fields::mk("eq");
    else if (*this == test) return fields::mk("test");
    else if (*this == test2) return fields::mk("test2");
    else if (*this == coordinator) return fields::mk("coordinator");
    else if (*this == compute) return fields::mk("compute");
    else if (*this == filesystem) return fields::mk("filesystem");
    else return "<bad type " + fields::mk(v) + ">"; }

const parser<interfacetype> &
interfacetype::parser() {
    return strmatcher("meta", meta) ||
        strmatcher("storage", storage) ||
        strmatcher("eq", eq) ||
        strmatcher("test2", test2) ||
        strmatcher("test", test) ||
        strmatcher("coordinator", coordinator) ||
        strmatcher("compute", compute) ||
        strmatcher("filesystem", filesystem); }
