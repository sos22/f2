#include "string.H"

#include <stdlib.h>
#include <string.h>

#include "fields.H"
#include "proto2.H"
#include "serialise.H"

const fields::strfield &
fields::mk(const string &s) {
    return mk(s.content); }

string::string()
    : content(NULL) {}

string
string::steal(char *&c) {
    string res;
    res.content = c;
    c = (char *)0xf001ul;
    return res; }

string::string(const char *c)
    : content(strdup(c)) {}

string::string(const string &o)
    : content(o.content
              ? strdup(o.content)
              : NULL) {}

string::string(string &&o)
    : content(o.content) {
    o.content = NULL; }

string::string(deserialise1 &ds) {
    size_t sz(ds);
    /* Avoid stupidity */
    if (sz > proto::maxmsgsize) {
        ds.fail(error::invalidmessage);
        content = strdup("<bad>");
        return; }
    auto c = (char *)malloc(sz + 1);
    ds.bytes(c, sz);
    c[sz] = '\0';
    content = c; }

void
string::operator=(const string &o) {
    free((void *)content);
    content = o.content
        ? (char *)strdup(o.content)
        : NULL; }

string
string::operator+(const string &o) const {
    size_t s1(strlen(c_str()));
    size_t s2(strlen(o.c_str()));
    if (s1 == 0 && s2 == 0) return string();
    char *r = (char *)malloc(s1 + s2 + 2);
    memcpy(r, c_str(), s1);
    memcpy(r + s1, o.c_str(), s2);
    r[s1+s2] = '\0';
    return steal(r); }

bool
string::operator==(const string &o) const {
    return !(*this != o); }

bool
string::operator!=(const string &o) const {
    return strcmp(c_str(), o.c_str()) != 0; }

bool
string::operator<(const string &o) const {
    return strcmp(c_str(), o.c_str()) < 0; }

bool
string::operator>(const string &o) const {
    return strcmp(c_str(), o.c_str()) > 0; }

size_t
string::len() const {
    return strlen(content); }

string::~string() {
    free((void *)content); }

void
string::serialise(serialise1 &s) {
    size_t sz(strlen(content));
    s.push(sz);
    s.bytes(content, sz); }

const char *
string::c_str() const {
    return content
        ? content
        : ""; }
