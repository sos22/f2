#include "bytecount.H"

#include "parsers.H"

#include "parsers.tmpl"

const ::parser<bytecount> &
bytecount::parser() {
    auto &d(parsers::intparser<unsigned long>());
    return
        (d + strmatcher("B")).map<bytecount>([] (const unsigned long &c) {
                return bytecount::bytes(c); }) ||
        (d + strmatcher("kB")).map<bytecount>([] (const unsigned long &c) {
                return bytecount::kilobytes(c); }) ||
        (d + strmatcher("MB")).map<bytecount>([] (const unsigned long &c) {
                return bytecount::megabytes(c); }) ||
        (d + strmatcher("kiB")).map<bytecount>([] (const unsigned long &c) {
                return bytecount::kibibytes(c); }) ||
        (d + strmatcher("MiB")).map<bytecount>([] (const unsigned long &c) {
                return bytecount::mebibytes(c); }) ||
        d.map<bytecount>([] (const unsigned long &c) {
                return bytecount::bytes(c); }); }
