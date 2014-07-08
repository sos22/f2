/* Temporary heaps are used to allocate space for temporary objects.
 * Something allocated in a temporary heap will remain valid until the
 * thread which did the allocation calls the heap's flush method. */
/* One major caveat: temporary heaps never run object destructors;
 * they just blindly reclaim the space.  Don't use them for things
 * with non-trivial destructors. */
#ifndef TMPHEAP_H__
#define TMPHEAP_H__

#include "pthread.h"

class tmpheap {
    /* This needs to be public so that __arena_destruct() can get at
       it (can't use friend because __arena_destruct() is static). */
public:  struct arena;
private: pthread_key_t arenakey;
public: tmpheap();

    /* Allocate @sz bytes in the heap and return a pointer.  The space
     * is initially uninitialised.  The space will remain valid until
     * this thread calls flush() or exits. */
public: void *_alloc(size_t sz);
    /* Allocate a size-@n array of instances of type in the heap. */
public: template <typename t> t *alloc(unsigned n = 1) {
    t *spc = (t *)_alloc(sizeof(t) * n);
    for (unsigned x = 0; x < n; x++) new (&spc[x]) t();
    return spc; }
    /* Release all of the space allocated by _alloc() or alloc() in
     * this thread since the last call to flush(). */
public: void flush();
    /* Tear down the tmpheap.  Space allocated by this thread will be
     * automatically released; any space allocated by other threads
     * and not yet released will be leaked.  Note that there is
     * absolutely no automatic checking that this is done correctly;
     * just be careful. */
    /* statically-allocated tmpheaps always get this right, because
     * they get destructed by the last surviving thread during process
     * shutdown and so there are no other thread which might cause a
     * leak. */
public: ~tmpheap();
};

/* A thing you can inherit from if you want all allocations of a
   particular type to come from a particular tmpheap. */
template <tmpheap &heap> class allocintmpheap {
public:  void *operator new(size_t sz) { return heap._alloc(sz); }
};

#endif /* !TMPHEAP_H__ */
