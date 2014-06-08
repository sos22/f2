#include "obj.H"

void *operator new(size_t , void *buf)
{
    return buf;
}
