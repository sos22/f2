#include "util.H"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

#include "error.H"
#include "orerror.H"

orerror<long>
parselong(const char *what)
{
    char *end;
    long res;
    errno = 0;
    res = strtol(what, &end, 0);
    if (end == what)
        return error::noparse;
    if (errno)
        return error::from_errno();
    while (isspace(*end))
        end++;
    if (*end != '\0')
        return error::noparse;
    return res;
}

template <typename t> t
loadacquire(const t &what) {
    t res;
    switch (sizeof(t)) {
    case 1:
        asm volatile ("movb %1,%0\n"
                      : "=r" (*(unsigned char *)&res)
                      : "m" (*(unsigned char *)&what)
                      : "memory");
        break;
    case 2:
        asm volatile ("movs %1,%0\n"
                      : "=r" (*(unsigned short *)&res)
                      : "m" (*(unsigned short *)&what)
                      : "memory");
        break;
    case 4:
        asm volatile ("movl %1,%0\n"
                      : "=r" (*(unsigned *)&res)
                      : "m" (*(unsigned *)&what)
                      : "memory");
        break;
    case 8:
        asm volatile ("movq %1,%0\n"
                      : "=r" (*(unsigned long *)&res)
                      : "m" (*(unsigned long*)&what)
                      : "memory");
        break;
    default: abort(); }
    return res; }

template <typename t> void
storerelease(t *where, t what) {
    switch (sizeof(what)) {
    case 1:
        asm volatile("movb %1, %0"
                     : "=m" (*(unsigned char *)where)
                     : "r" ((unsigned char)what)
                     : "memory");
        break;
    case 2:
        asm volatile("movs %1, %0"
                     : "=m" (*(unsigned short *)where)
                     : "r" ((unsigned short)what)
                     : "memory");
        break;
    case 4:
        asm volatile("movl %1, %0"
                     : "=m" (*(unsigned *)where)
                     : "r" ((unsigned)what)
                     : "memory");
        break;
    case 8:
        asm volatile("movq %1, %0"
                     : "=m" (*(unsigned long *)where)
                     : "r" ((unsigned long)what)
                     : "memory");
        break;
    default:
        abort(); } }

template <typename t> t
atomicloaddec(t &what) { return __sync_fetch_and_sub(&what, 1); }

template <typename t> void
atomicinc(t &what) { __sync_fetch_and_add(&what, 1); }

template <typename t> t
atomicload(const t &what) {
    return __sync_fetch_and_add((t *)&what, 0); }

template int loadacquire(const int &);
template bool loadacquire(const bool &);
template void storerelease(bool *, bool);
template unsigned atomicload(const unsigned &);
template void atomicinc(unsigned &);
template unsigned atomicloaddec(unsigned &);

void
mb() { asm volatile("mfence\n"); }
