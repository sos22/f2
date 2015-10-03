#include "serialise.H"

#include "buffer.H"
#include "quickcheck.H"

#include "either.tmpl"
#include "orerror.tmpl"

deserialise1::deserialise1(const buffer &_src)
    : src(left<nnp<quickcheck> >(_nnp(_src))),
      error(Success),
      _offset(_src.offset()) {}

deserialise1::deserialise1(quickcheck &_src)
    : src(right<nnp<const buffer> >(_nnp(_src))),
      error(Success),
      _offset(0) {}

bool
deserialise1::random() const { return src.isright(); }

deserialise1::operator bool() {
    if (random()) return *src.right();
    char c(*this);
    if (c == 0) return false;
    else if (c == 1) return true;
    else {
        fail(error::invalidmessage);
        return false; } }
deserialise1::operator char() { return pop<char>(); }
deserialise1::operator unsigned char() { return pop<unsigned char>(); }
deserialise1::operator short() { return pop<short>(); }
deserialise1::operator unsigned short() { return pop<unsigned short>(); }
deserialise1::operator int() { return pop<int>(); }
deserialise1::operator unsigned int() { return pop<unsigned int>(); }
deserialise1::operator long() { return pop<long>(); }
deserialise1::operator unsigned long() { return pop<unsigned long>(); }
deserialise1::operator long double() { return pop<long double>(); }

void
deserialise1::bytes(void *buf, size_t s) {
    if (src.isright()) {
        auto c = (unsigned char *)buf;
        for (size_t x = 0; x < s; x++) c[x] = *src.right(); }
    else if (_offset + s > src.left()->offset() + src.left()->avail()) {
        fail(::error::underflowed);
        memset(buf, 0, s); }
    else {
        memcpy(buf,
               src.left()->linearise(_offset, _offset + s),
               s);
        _offset += s; } }

template <typename t> t
deserialise1::pop() {
    if (random()) return (t)*src.right();
    t res;
    bytes(&res, sizeof(res));
    return res; }

void
deserialise1::fail(::error e) { if (error == Success) error = e; }

orerror<void>
deserialise1::status() const { return error; }

serialise1::serialise1(buffer &_dest) : dest(_dest) {}

void
serialise1::bytes(const void *buf, size_t sz) { dest.queue(buf, sz); }

template <typename t> void
serialise1::pushfundamental(t what) { bytes(&what, sizeof(what)); }

template <> void
serialise1::pushfundamental(bool b) {
    if (b) pushfundamental<char>(1);
    else pushfundamental<char>(0); }

template <> void
serialise1::push(const bool &x) { pushfundamental(x); }
template <> void
serialise1::push(const char &x) { pushfundamental(x); }
template <> void
serialise1::push(const unsigned char &x) { pushfundamental(x); }
template <> void
serialise1::push(const short &x) { pushfundamental(x); }
template <> void
serialise1::push(const unsigned short &x) { pushfundamental(x); }
template <> void
serialise1::push(const int &x) { pushfundamental(x); }
template <> void
serialise1::push(const unsigned int &x) { pushfundamental(x); }
template <> void
serialise1::push(const long &x) { pushfundamental(x); }
template <> void
serialise1::push(const unsigned long &x) { pushfundamental(x); }
template <> void
serialise1::push(const long double &x) { pushfundamental(x); }
