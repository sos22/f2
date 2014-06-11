#include "tid.H"

#include <sys/syscall.h>
#include <unistd.h>

tid::tid(unsigned _val)
    : val(_val)
{}

tid
tid::me()
{
    return tid(syscall(SYS_gettid));
}

bool
tid::operator ==(const tid &o) const
{
    return val == o.val;
}
