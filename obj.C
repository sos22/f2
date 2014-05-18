#include "obj.H"

void *operator new(size_t sz, void *buf)
{
    return buf;
}

