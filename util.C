#include "util.H"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

#include <valgrind/helgrind.h>

#include "error.H"
#include "fields.H"
#include "orerror.H"

#include "orerror.tmpl"

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
racey<t>::load() const {
    t res;
    switch (sizeof(t)) {
    case 1:
        asm volatile ("movb %1,%0\n"
                      : "=r" (*(unsigned char *)&res)
                      : "m" (*(unsigned char *)&content)
                      : "memory");
        break;
    case 2:
        asm volatile ("movs %1,%0\n"
                      : "=r" (*(unsigned short *)&res)
                      : "m" (*(unsigned short *)&content)
                      : "memory");
        break;
    case 4:
        asm volatile ("movl %1,%0\n"
                      : "=r" (*(unsigned *)&res)
                      : "m" (*(unsigned *)&content)
                      : "memory");
        break;
    case 8:
        asm volatile ("movq %1,%0\n"
                      : "=r" (*(unsigned long *)&res)
                      : "m" (*(unsigned long*)&content)
                      : "memory");
        break;
    default: abort(); }
    ANNOTATE_HAPPENS_AFTER(&content);
    return res; }

template <typename t> void
racey<t>::store(t what) {
    ANNOTATE_HAPPENS_BEFORE(&content);
    switch (sizeof(t)) {
    case 1:
        asm volatile("movb %1, %0"
                     : "=m" (*(unsigned char *)&content)
                     : "r" ((unsigned char)what)
                     : "memory");
        break;
    case 2:
        asm volatile("movs %1, %0"
                     : "=m" (*(unsigned short *)&content)
                     : "r" ((unsigned short)what)
                     : "memory");
        break;
    case 4:
        asm volatile("movl %1, %0"
                     : "=m" (*(unsigned *)&content)
                     : "r" ((unsigned)what)
                     : "memory");
        break;
    case 8:
        asm volatile("movq %1, %0"
                     : "=m" (*(unsigned long *)&content)
                     : "r" ((unsigned long)what)
                     : "memory");
        break;
    default:
        abort(); } }

template <typename t> t racey<t>::fetchadd(t delta) {
    ANNOTATE_HAPPENS_BEFORE(&content);
    t res = __sync_fetch_and_add(&content, delta);
    ANNOTATE_HAPPENS_AFTER(&content);
    return res; }

template <typename t> t
atomicloaddec(t &what) { return __sync_fetch_and_sub(&what, 1); }

template <typename t> void
atomicinc(t &what) { __sync_fetch_and_add(&what, 1); }

template <typename t> t
atomicswap(t &what, t n) {
    t old(what);
    while (true) {
        auto nv = __sync_val_compare_and_swap(&what, old, n);
        if (nv == old) return old;
        old = nv; } }

template <typename t>
const fields::field &racey<t>::field() const { return fields::mk(load()); }

template void atomicinc(unsigned &);
template void atomicinc(unsigned long &);
template unsigned atomicloaddec(unsigned &);
template unsigned atomicswap(unsigned &, unsigned);
template unsigned long atomicswap(unsigned long &, unsigned long);

template class racey<bool>;
template class racey<int>;
template class racey<unsigned>;
template class racey<unsigned long>;

void
mb() { asm volatile("mfence\n"); }
