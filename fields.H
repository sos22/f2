/* Various bits of gubbins useful for pretty-printing structures in a
 * reasonably standardised way. */
#ifndef FIELDS_H__
#define FIELDS_H__

#include <sys/types.h>

#include "maybe.H"
#include "tid.H"

class test;

namespace fields {

class fieldbuf {
    friend void flush();
    tid owner;
    fieldbuf *next;
    struct fragment {
        struct fragment *next;
        unsigned short used;
        char content[4086];
    };
    fragment *head;
    fragment *tail;
public:
    fieldbuf();
    ~fieldbuf();
    void reset();
    char *c_str(maybe<unsigned> limit = Nothing);
    void push(const char *);
};

class field {
    field(const field &) = delete;
    void operator=(const field &) = delete;
protected:
    field();
    static void *operator new(size_t sz);
    virtual ~field() {}
public:
    virtual void fmt(fieldbuf &) const = 0;
};

extern const field &space;

const field &operator +(const field &, const field &);

const field &trunc(const field &,
                   unsigned maxsize);
const field &padleft(const field &,
                     unsigned minsize,
                     const field &pad = space);
const field &padright(const field &,
                      unsigned minsize,
                      const field &pad = space);
const field &padcenter(const field &,
                       unsigned minsize,
                       const field &padleft = space,
                       const field &padright = space);

const field &mk(const char *);

class intfield : public field {
    long val_;
    int base_;
    bool sep_;
    bool uppercase_;
    bool alwayssign_;
    intfield(long _val, int _base, bool _sep, bool _uppercase,
             bool _alwayssign);
public:
    static const intfield &n(long val, int base, bool sep,
                             bool uppercase, bool alwayssign);
    const intfield &base(int b) const;
    const intfield &nosep() const;
    const intfield &uppercase() const;
    const intfield &alwayssign() const;
    void fmt(fieldbuf &) const;
};
const intfield &mk(long);

class timefield : public field {
    struct timeval v;
    bool asdate_;
    timefield(const struct timeval &_v,
              bool _asdate);
public:
    static const timefield &n(const struct timeval &,
                              bool asdate);
    const timefield &asdate() const;
    void fmt(fieldbuf &) const;
};
const timefield &mk(const struct timeval &);

void flush();

void test(class ::test &);
};

#endif /* !FIELDS_H__ */
