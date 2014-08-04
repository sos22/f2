#include "quickcheck.H"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>

#include "tmpheap.H"

quickcheck::operator unsigned long() const {
    unsigned long r = random();
    if (r % 8 == 0) {
        /* Pick an interesting number. */
        r /= 8;
        switch (r % 13) {
        case 0: return 0;
        case 1: return 1;
        case 2: return -1l;
        case 3: return 2;
        case 4: return -2l;
        case 5: return 0xff;
        case 6: return 0xffff;
        case 7: return 10;
        case 8: return 100;
        case 9: return -10l;
        case 10: return -100l;
        case 11: return (1ul << (random() % 64));
        case 12: return (1ul << (random() % 64)) - 64 + (random() % 64);
        default: abort(); }
    } else {
        r /= 8;
        r ^= random() * (1ul << 16);
        r ^= random() * (1ul << 32);
        r ^= random() * (1ul << 48);
        return r; } }

quickcheck::operator long() const {
    return (long)(unsigned long)*this; }

quickcheck::operator unsigned() const {
    return (unsigned)(unsigned long)*this; }
quickcheck::operator int() const {
    return (int)(unsigned long)*this; }

quickcheck::operator unsigned short() const {
    return (unsigned short)(unsigned long)*this; }

quickcheck::operator bool() const {
    return random() % 2 == 0; }

quickcheck::operator double() const {
    /* Standard Cauchy distribution, because why not? */
    return tan(M_PI * (drand48() - .5)); }

quickcheck::operator const char *() const {
    unsigned long r = random();
    if (r % 4 == 0) return "";
    r /= 4;
    long len;
    len = (1 << (r % 16)) + ((random() % 256) - 128);
    while (len <= 0) {
        r = random();
        len = (1 << (r % 16)) + (((r / 16) % 256) - 128); }
    char *buf = (char *)tmpheap::_alloc(len);
    for (int x = 0; x < len - 1; x++) buf[x] = (char)((random() % 255) + 1);
    buf[len] = '\0';
    return (const char *)buf; }

const char *
quickcheck::filename() const {
    int len = ((unsigned)random() % 255) + 1;
    char *buf = (char *)tmpheap::_alloc(len + 1);
    for (int x = 0; x < len - 1; x++) {
        char c;
        do {
            c = (char)(random() % 255 + 1);
        } while (c == '/' || !isprint(c));
        buf[x] = c; }
    buf[len] = 0;
    return buf; }
