/* The temporary heap is used for allocating thread-private things
   which don't need to live for very long.  Each thread effectively
   maintains its own heap and can allocate freely from it.  Every so
   often, the thread is supposed to call tmpheap::release(), at which
   point everything it has allocated in its temp heap will be
   released.  That's usually a bit easier to deal with than calling
   free()/delete all over the place, and is also often faster.  The
   major downside, apart from losing control over storage lifetime, is
   that destructors never run, so you can't use this for anything with
   a non-trivial destructor. */
#ifndef TMPHEAP_H__
#define TMPHEAP_H__

#include <stddef.h>

namespace tmpheap {

/* Allocate some bytes.  The returned pointer remains valid until the
 * allocating thread calls release() or exits. */
void *_alloc(size_t sz);
/* Allocate a particular type and run its constructor.  The destructor
 * will never be run, so only use this for types where the destructor
 * is trivial. */
template <typename t, typename ... args> t *alloc(args...params) {
    void *spc = _alloc(sizeof(t));
    return new (spc) t(params...); }
/* Release everything allocated by _alloc() or alloc() in the current
 * thread.  Called automatically as part of thread tear-down. */
void release();

/* Convenience function: duplicate a string into the temporary
 * heap. */
char *strdup(const char *);
/* Convenience function: duplicate a block of memory into the
 * temporary heap. */
void *memdup(const void *base, size_t sz);
/* Convenience function: duplicate an object into the temporary heap,
 * using its copy constructor.  Really just aliases for alloc(); use
 * whichever makes most logical sense. */
template <typename t> t *objdup(const t &&x) { return alloc(x); }
template <typename t> t *objdup(const t &x) { return alloc(x); }

/* Base class which, when inherited from, causes all allocations of a
 * particular type to be places in the temporary heap.  Note that this
 * only catches new-style allocation, not stack-style, and that there
 * are no checks that it isn't used for a stack allocation. */
class base {
public:  void *operator new(size_t sz) { return _alloc(sz); }
};

}

#endif /* !TMPHEAP_H__ */
