#include "futex.H"

#include <asm/unistd.h>
#include <linux/futex.h>
#include <errno.h>
#include <limits.h>

#include <valgrind/valgrind.h>

#include "clientio.H"
#include "timedelta.H"
#include "timestamp.H"

#include "logging.H"
#include "fields.tmpl"

namespace {
/* true on success, false on timeout */
static bool syswait(const unsigned *what,
                    unsigned val,
                    maybe<timestamp> deadline) {
    struct timespec ts;
    struct timespec *tsp;
    if (deadline == Nothing) tsp = NULL;
    else {
        auto n(timestamp::now());
        if (deadline.just() < n) return false;
        ts = ((deadline.just() - n) *
              (RUNNING_ON_VALGRIND ? VALGRIND_TIMEWARP : 1)).astimespec();
        tsp = &ts; }
    register long rax asm("rax");
    do{ register unsigned long r11 asm("r11");
        register unsigned long rcx asm("rcx");
        register unsigned long rdi asm("rdi") = (unsigned long)what;
        register unsigned long rsi asm("rsi") = FUTEX_WAIT_PRIVATE;
        register unsigned long rdx asm("rdx") = val;
        register unsigned long r10 asm("r10") = (unsigned long)tsp;
        rax = __NR_futex;
        asm("syscall\n"
            : "=r" (rcx),
              "=r" (r11),
              "=r" (rax)
            : "r" (rax),
              "r" (rdi),
              "r" (rsi),
              "r" (rdx),
              "r" (r10)
            : "memory");
    } while (rax == -EINTR);
    assert(rax == 0 || rax == -ETIMEDOUT || rax == -EAGAIN);
    return rax != -ETIMEDOUT; }
static void syswake(const unsigned *what) {
    register unsigned long r11 asm("r11");
    register unsigned long rcx asm("rcx");
    register unsigned long rdi asm("rdi") = (unsigned long)what;
    register unsigned long rsi asm("rsi") = FUTEX_WAKE_PRIVATE;
    register unsigned long rdx asm("rdx") = INT_MAX;
    register long rax asm("rax") = __NR_futex;
    asm("syscall\n"
        : "=r" (rcx),
          "=r" (r11),
          "=r" (rax)
        : "r" (rax),
          "r" (rdi),
          "r" (rsi),
          "r" (rdx)
        : "memory");
    assert(rax >= 0); } }

void futex::set(unsigned x) {
    inner.store(x);
    syswake(&inner.content); }

unsigned futex::fetchadd(unsigned x) {
    unsigned r = inner.fetchadd(x);
    syswake(&inner.content);
    return r; }

unsigned futex::poll() const { return inner.load(); }

maybe<unsigned> futex::wait(clientio, unsigned wh, maybe<timestamp> ts) const {
    while (true) {
        unsigned r = inner.load();
        if (r != wh) return r;
        if (!syswait(&inner.content, wh, ts)) return Nothing; } }

unsigned futex::wait(clientio io, unsigned wh) const {
    auto r(wait(io, wh, Nothing));
    assert(r.isjust());
    return r.just(); }

const fields::field &futex::field() const { return "F:" + inner.field(); }
