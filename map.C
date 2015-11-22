#include "map.H"

#include "string.H"

template <> unsigned long
default_hashfn<int>(const int &what) { return what; }

template <> unsigned long
default_hashfn<unsigned int>(const unsigned int &what) { return what; }

template <> unsigned long
default_hashfn<unsigned long>(const unsigned long &what) { return what; }

template <> unsigned long
default_hashfn<const char *>(const char *const &c) {
    ssize_t l(strlen(c));
    unsigned long acc(0);
    ssize_t i = 0;
    while (i <= l - 8) {
        acc = acc * 7 + *((unsigned long *)(c + i / 8));
        i += 8; }
    unsigned long a(0);
    while (i < l) {
        a = a * 256 + c[i];
        i++; }
    acc = acc * 7 + a;
    return acc; }

template <> unsigned long
default_hashfn<char *>(char *const &c) {
    return default_hashfn<const char *>(c); }

template <> bool
map<int, int, default_hashfn>::isprime(unsigned what) {
    if (what < 2) return false;
    for (unsigned x = 2; x * x <= what; x++) if (what % x == 0) return false;
    return true; }

/* Rebuild the hash table with a different number of buckets. */
template <> void
map<int, int, default_hashfn>::rehash(unsigned newnrbuckets) {
    auto newbuckets(new entry *[newnrbuckets]);
    memset(newbuckets, 0, sizeof(newbuckets[0]) * newnrbuckets);
    for (unsigned x = 0; x < nrbuckets; x++) {
        auto b(buckets[x]);
        while (b != NULL) {
            auto next(b->next);
            b->next = newbuckets[b->h % newnrbuckets];
            newbuckets[b->h % newnrbuckets] = b;
            b = next; } }
    delete [] buckets;
    buckets = newbuckets;
    nrbuckets = newnrbuckets; }

/* Given that we suspect @cursize is too small, pick a sensible new
 * size.  We always pick primes, because that helps to mask problems
 * with weak hash functions, and we try to grow by about a factor of
 * eight each time, because rehashing is expensive.  The first few
 * sizes are all hard-coded, to avoid doign expensive primality tests
 * too often. */
template <> unsigned
map<int, int, default_hashfn>::nextsize(unsigned oldsize) {
    switch (oldsize) {
    case 0:    return 7;
    case 7:    return 59;
    case 59:   return 479;
    case 479:  return 3833;
    case 3833: return 30671;
    default:
        /* We should always hit one of the hard-coded ones when we're
         * in range. */
        assert(oldsize >= 30671);
        for (unsigned x = oldsize * 8; true; x++) if (isprime(x)) return x; } }

/* Inverse for nextsize() */
template <> unsigned
map<int, int, default_hashfn>::prevsize(unsigned oldsize) {
    switch (oldsize) {
    case 7:
        /* Don't shrink below size 7. */
        return 7;
    case 59: return 7;
    case 479: return 59;
    case 3833: return 479;
    case 30671: return 3833;
    default:
        assert(oldsize > 30671);
        for (unsigned x = oldsize / 8; true; x--) if (isprime(x)) return x; } }
