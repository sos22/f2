#include "tid.H"

#include <sys/syscall.h>
#include <unistd.h>

#include "fields.H"

static __thread unsigned me;

tid::tid(unsigned _val) : val(_val) {}

tid tid::me() {
    if (::me == 0) {
        auto v(syscall(SYS_gettid));
        assert(v == (unsigned)v);
        ::me = (unsigned)v; }
    return tid(::me); }

tid
tid::nonexistent() { return tid(-1); }

bool
tid::operator ==(const tid &o) const
{
    return val == o.val;
}

const fields::field &
tid::field() const { return "t:" + fields::mk(val).nosep(); }
