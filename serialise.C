#include "serialise.H"

#include "buffer.H"

#include "either.tmpl"

deserialise1::deserialise1(const buffer &_src)
    : src(left<nnp<quickcheck> >(_nnp(_src))),
      error(Success),
      _offset(0) {}

deserialise1::deserialise1(quickcheck &_src)
    : src(right<nnp<const buffer> >(_nnp(_src))),
      error(Success),
      _offset(0) {}

deserialise1::operator bool() {
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

template <typename t> t
deserialise1::pop() {
    if (src.isright()) return t(*src.right());
    else if (_offset + sizeof(t) > src.left()->offset() + src.left()->avail()) {
        fail(::error::underflowed);
        return t(0); }
    else {
        t res(*src.left()->linearise<t>(_offset));
        _offset += sizeof(t);
        return res; } }

void
deserialise1::fail(::error e) { if (error == Success) error = e; }

orerror<void>
deserialise1::status() const { return error; }

serialise1::serialise1(buffer &_dest) : dest(_dest) {}

template <typename t> void
serialise1::pushfundamental(t what) { dest.queue(&what, sizeof(what)); }
template <> void
serialise1::pushfundamental(bool b) {
    if (b) pushfundamental<char>(1);
    else pushfundamental<char>(0); }

void
serialise1::push(bool x) { pushfundamental(x); }
void
serialise1::push(char x) { pushfundamental(x); }
void
serialise1::push(unsigned char x) { pushfundamental(x); }
void
serialise1::push(short x) { pushfundamental(x); }
void
serialise1::push(unsigned short x) { pushfundamental(x); }
void
serialise1::push(int x) { pushfundamental(x); }
void
serialise1::push(unsigned int x) { pushfundamental(x); }
void
serialise1::push(long x) { pushfundamental(x); }
void
serialise1::push(unsigned long x) { pushfundamental(x); }
