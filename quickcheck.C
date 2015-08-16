#include "quickcheck.H"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tmpheap.H"

quickcheck::operator unsigned long() const {
    unsigned long r = (unsigned long)random();
    if (r % 8 == 0) {
        /* Pick an interesting number. */
        r /= 8;
        switch (r % 13) {
        case 0: return 0;
        case 1: return 1;
        case 2: return (unsigned long)-1l;
        case 3: return 2;
        case 4: return (unsigned long)-2l;
        case 5: return 0xff;
        case 6: return 0xffff;
        case 7: return 10;
        case 8: return 100;
        case 9: return (unsigned long)-10l;
        case 10: return (unsigned long)-100l;
        case 11: return (1ul << (random() % 64));
        case 12: return (1ul << (random() % 64)) - 64 + (random() % 64);
        default: abort(); }
    } else {
        r /= 8;
        r ^= (unsigned long)random() << 16;
        r ^= (unsigned long)random() << 32;
        r ^= (unsigned long)random() << 48;
        return r; } }

quickcheck::operator long() const {
    return (long)(unsigned long)*this; }

quickcheck::operator unsigned() const {
    return (unsigned)(unsigned long)*this; }
quickcheck::operator int() const {
    return (int)(unsigned long)*this; }

quickcheck::operator unsigned short() const {
    return (unsigned short)(unsigned long)*this; }

quickcheck::operator short() const {
    return (short)(unsigned long)*this; }

quickcheck::operator char() const {
    return (char)(unsigned long)*this; }

quickcheck::operator unsigned char() const {
    return (unsigned char)(unsigned long)*this; }

quickcheck::operator bool() const {
    return random() % 2 == 0; }

quickcheck::operator double() const {
    /* Standard Cauchy distribution, because why not? */
    return tan(M_PI * (drand48() - .5)); }

quickcheck::operator long double() const {
    return tanl(M_PI * (drand48() - .5)); }

quickcheck::operator const char *() const {
    unsigned long r = (unsigned long)random();
    switch (r % 64) {
        /* A couple of interesting fixed strings. */
    case 0: return "";
    case 1: return " ";
    case 2: return "<";
    case 3: return ":";
    case 4: return "\"";
    case 5: return "'";
    case 6: return "\t";
    case 7: return "\r";
    case 8: return "\n";
    case 9: return "\r\n";
    case 10: return "0";
    case 11: return "x";
    case 12: return "_";
    case 13: return "-";
    default:
        unsigned long len;
        if (r % 64 < 60) {
            /* Small strings */
            len = (r / 64) % 16 + 1; }
        else {
            /* Big strings */
            r /= 64;
            len = (1 << (r % 16)) + (((r >> 21) % 256) - 128);
            while ((long)len <= 0) {
                r = (unsigned long)random();
                len = (1 << (r % 16)) + (((r / 16) % 256) - 128); } }
        char *buf = (char *)tmpheap::_alloc(len+1);
        for (unsigned x = 0; x < len - 1; x++) {
            buf[x] = (char)((random() % 255) + 1); }
        buf[len] = '\0';
        return (const char *)buf; }
    abort(); }

const char *
quickcheck::filename() const {
    char *buf;
    do {
        unsigned len = ((unsigned)random() % 255) + 5;
        buf = (char *)tmpheap::_alloc(len + 1);
        strcpy(buf, "tmp/");
        for (unsigned x = 4; x < len - 1; x++) {
            char c;
            do {
                c = (char)(random() % 255 + 1);
            } while (c == '/' || !isprint(c));
            buf[x] = c; }
        buf[len] = 0;
    } while (!strcmp(buf, ".") || !strcmp(buf, ".."));
    return buf; }
