#include "string.H"

#include <stdlib.h>
#include <string.h>

#include "fields.H"
#include "map.H"
#include "parsers.H"
#include "proto2.H"
#include "quickcheck.H"
#include "serialise.H"

#include "parsers.tmpl"
#include "serialise.tmpl"

const fields::strfield &
fields::mk(const string &s) { return mk(s.content ?: "").escape(); }

string::string()
    : content(NULL) {}

string
string::steal(char *&c) {
    string res;
    if (c != NULL && c[0] == '\0') res.content = NULL;
    else res.content = c;
    c = (char *)0xf001ul;
    return res; }

string::string(const char *c)
    : content(c == NULL ? NULL : strdup(c)) {}

string::string(const string &o)
    : content(o.content
              ? strdup(o.content)
              : NULL) {}

string::string(string &&o) : string(Steal, o) {}

string::string(_Steal, string &o) : content(o.content) { o.content = NULL; }

string::string(deserialise1 &ds) {
    if (ds.random()) content = strdup((const char *)quickcheck());
    else {
        size_t sz(ds.poprange<size_t>(0, proto::maxmsgsize));
        /* Avoid stupidity */
        if (sz > proto::maxmsgsize) {
            ds.fail(error::invalidmessage);
            content = strdup("<bad>");
            return; }
        if (sz == 0) content = NULL;
        else {
            auto c = (char *)malloc(sz + 1);
            ds.bytes(c, sz);
            c[sz] = '\0';
            content = c; } } }

void
string::operator=(const string &o) {
    free((void *)content);
    content = o.content != NULL
        ? (char *)strdup(o.content)
        : NULL; }

void
string::operator+=(const char *o) {
    size_t olen(strlen(o));
    if (olen == 0) {}
    else if (content == NULL) content = strdup(o);
    else {
        size_t l(strlen(content));
        content = (char *)realloc(content, l + olen + 1);
        memcpy(content + l, o, olen + 1); } }

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
string::operator<(const string &o) const {
    return strcmp(c_str(), o.c_str()) < 0; }

bool
string::operator<=(const string &o) const {
    return strcmp(c_str(), o.c_str()) <= 0; }

bool
string::operator==(const string &o) const {
    return strcmp(c_str(), o.c_str()) == 0; }

bool
string::operator!=(const string &o) const {
    return strcmp(c_str(), o.c_str()) != 0; }

bool
string::operator>=(const string &o) const {
    return strcmp(c_str(), o.c_str()) >= 0; }

bool
string::operator>(const string &o) const {
    return strcmp(c_str(), o.c_str()) > 0; }

size_t
string::len() const {
    return strlen(c_str()); }

void
string::truncate(size_t sz) {
    assert(len() >= sz);
    content[sz] = '\0'; }

string::~string() {
    free((void *)content); }

void
string::serialise(serialise1 &s) const {
    size_t sz(content == NULL ? 0 : strlen(content));
    s.push(sz);
    s.bytes(content, sz); }

const char *
string::c_str() const {
    return content
        ? content
        : ""; }

maybe<string>
string::stripprefix(const string &other) const {
    auto t(c_str());
    auto o(other.c_str());
    unsigned x;
    for (x = 0; t[x] != '\0' && o[x] == t[x]; x++) ;
    if (o[x] == '\0') return string(t + x);
    else return Nothing; }

const parser<string> &
string::parser(void) {
    class f : public ::parser<string> {
    public: const ::parser<const char *> &inner;
    public: f() : inner(parsers::strparser) {}
    public: orerror<result> parse(const char *what) const {
        return inner.parse(what)
            .map<parser<string>::result>([] (auto res) {
                    return res.map<string>([] (auto res2) {
                            return string(res2); }); }); } };
    return *new f(); }

unsigned long
string::hash() const {
    if (content == NULL) return 0;
    else return default_hashfn(content); }
