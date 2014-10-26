#include "serialise.H"

#include "buffer.H"

#include "either.tmpl"

deserialise1::deserialise1(buffer &_src)
    : src(left<nnp<quickcheck> >(_nnp(_src))),
      error(Success) {}

deserialise1::deserialise1(quickcheck &_src)
    : src(right<nnp<buffer> >(_nnp(_src))),
      error(Success) {}

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
    else if (src.left()->avail() < sizeof(t)) {
        fail(::error::underflowed);
        return t(0); }
    else {
        t res;
        src.left()->fetch(&res, sizeof(res));
        return res; } }

void
deserialise1::fail(::error e) { if (error == Success) error = e; }

orerror<void>
deserialise1::status() const { return error; }

serialisebase::serialisebase(buffer &_dest) : dest(_dest) {}

serialise1::serialise1(buffer &_dest) : serialisebase(_dest) { push<short>(1); }

serialise1::serialise1(short version, buffer &_dest)
    : serialisebase(_dest) { push(version); }

template <typename t> void
serialisebase::push(t what) { dest.queue(&what, sizeof(what)); }

template <> void
serialisebase::push(bool b) {
    if (b) push<char>(1);
    else push<char>(0); }

template void serialisebase::push(long);
template void serialisebase::push(int);
template void serialisebase::push(short);
template void serialisebase::push(char);
template void serialisebase::push(unsigned long);
template void serialisebase::push(unsigned int);
template void serialisebase::push(unsigned short);
template void serialisebase::push(unsigned char);
