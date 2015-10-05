#include "tid.H"

#include <sys/syscall.h>
#include <unistd.h>

#include "fields.H"

tid::tid(unsigned _val)
    : val(_val)
{}

tid
tid::me()
{
    auto v(syscall(SYS_gettid));
    assert(v == (unsigned)v);
    return tid((unsigned)v);
}

bool
tid::operator ==(const tid &o) const
{
    return val == o.val;
}

const fields::field &
tid::field() const { return "t:" + fields::mk(val).nosep(); }
