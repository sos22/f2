#include "tmpheap.H"

#include <stdlib.h>
#include <string.h>

#include "error.H"

struct tmpheap::arena {
    arena *next;
    unsigned size;
    unsigned used;
    unsigned char content[];
};

static void
__arena_destruct(void *_arena) {
    tmpheap::arena *arenas = (tmpheap::arena *)_arena;
    while (arenas) {
        auto n(arenas->next);
        free(arenas);
        arenas = n; } }

tmpheap::tmpheap() {
    int err = pthread_key_create(&arenakey, __arena_destruct);
    if (err) error::from_errno(err).fatal("creating tmpheap arena key"); }

void *
tmpheap::_alloc(size_t sz) {
    /* Keep everything 16-byte aligned, for general sanity. */
    arena *arenas = (arena *)pthread_getspecific(arenakey);
    sz = (sz + 15) & ~15ul;
    if (!arenas || arenas->used + sz > arenas->size) {
        size_t size;
        for (size = 8192;
             size - 32 - sizeof(arena) < sz;
             size *= 4)
            ;
        arena *a = (arena *)malloc(size - 32);
        a->next = arenas;
        a->size = size - 32 - sizeof(*a);
        a->used = 0;
        memset(a->content, 'Z', a->size);
        pthread_setspecific(arenakey, a);
        arenas = a; }
    void *res = &arenas->content[arenas->used];
    arenas->used += sz;
    return res; }

void
tmpheap::flush() {
    struct arena *arenas = (arena *)pthread_getspecific(arenakey);
    while (arenas) {
        auto n(arenas->next);
        free(arenas);
        arenas = n; }
    pthread_setspecific(arenakey, NULL); }

tmpheap::~tmpheap() {
    flush();
    pthread_key_delete(arenakey); }
