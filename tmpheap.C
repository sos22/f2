#include "tmpheap.H"

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "error.H"

namespace tmpheap {

struct arena {
    arena *next;
    unsigned size;
    unsigned used;
    unsigned char content[];
};

static pthread_key_t arenakey;

static struct inittmpheap_ {
    static void endthread(void *_arenas) {
        arena *arenas = (arena *)_arenas;
        while (arenas) {
            auto n(arenas->next);
            free(arenas);
            arenas = n; } }
    inittmpheap_() {
        int err = pthread_key_create(&arenakey, endthread);
        if (err) error::from_errno(err).fatal("creating tmpheap arena key"); }
} inittmpheap;

void *
_alloc(size_t sz) {
    arena *arenas = (arena *)pthread_getspecific(arenakey);
    /* Keep everything 16-byte aligned, for general sanity. */
    sz = (sz + 15) & ~15ul;
    if (!arenas || arenas->used + sz > arenas->size) {
        size_t size;
        /* -32 is because malloc() is often particular inefficient for
           sizes which are a power of two.  XXX not actually sure if
           this helps at all. */
        for (size = 8192;
             size - 32 - sizeof(arena) < sz;
             size *= 4)
            ;
        arena *a = (arena *)malloc(size - 32);
        a->next = arenas;
        a->size = (unsigned)(size - 32 - sizeof(*a));
        assert(a->size == (unsigned)(size - 32 - sizeof(*a)));
        a->used = 0;
        memset(a->content, 'Z', a->size);
        pthread_setspecific(arenakey, a);
        arenas = a; }
    void *res = &arenas->content[arenas->used];
    assert(arenas->used + sz == (unsigned)(arenas->used + sz));
    arenas->used += (unsigned)sz;
    return res; }

void
release() {
    struct arena *arenas = (arena *)pthread_getspecific(arenakey);
    while (arenas) {
        auto n(arenas->next);
        free(arenas);
        arenas = n; }
    pthread_setspecific(arenakey, NULL); }

char *
strdup(const char *x) {
    return (char *)memdup(x, strlen(x) + 1); }

void *
memdup(const void *base, size_t sz) {
    void *spc = _alloc(sz);
    memcpy(spc, base, sz);
    return spc; }
}
