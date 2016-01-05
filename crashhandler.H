#ifndef CRASHHANDLER_H__
#define CRASHHANDLER_H__

#include <sys/types.h>

namespace fields { class field; }

class crashhandler;

/* Token type to indicate that something's called in a crash handler
 * context. */
class crashcontext {
    friend class crashhandler;
private: crashcontext() {} };

/* Callbacks which get invoked when we know we're going to crash, to
 * collect any interesting logging. There can be any manner of weird
 * memory corruption when these get invoked, and there might certainly
 * be any set of locks held. The crash handlers are all invoked in
 * newly-fork()ed processes, so you're effectively
 * single-threaded. There's a one second timeout on each one; the
 * expectation is that the vast majority will complete more quickly
 * than that. */
class crashhandler {
private:   crashhandler *next;
private:   crashhandler *prev;
private:   char *name;
protected: explicit crashhandler(const fields::field &_name);
protected: ~crashhandler();
private:   crashhandler(const crashhandler&) = delete;
private:   void operator=(const crashhandler &) = delete;
private:   virtual void doit(crashcontext) = 0;
private:   static void surrogate();
private:   static void *_allocshared(size_t);
private:   static void _releaseshared(void *, size_t);
    /* Normally, crash handlers run in their own address space, so
     * they can't interfere with each other or with the main process,
     * but it's occasionally useful to have a little bit of shared
     * state. allocshared allocates such state. */
public:    template <typename t, typename ... args> static t&
    allocshared(args&&...);
    /* Release a t allocated by allocshared, after running its
     * destructor. */
public:    template <typename t> static void releaseshared(t &);
public:    static void invoke();
public:    static bool crashing(); };

#endif /* !CRASHHANDLER_H__ */
