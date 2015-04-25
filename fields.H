/* Various bits of gubbins useful for pretty-printing structures in a
 * reasonably standardised way. */
#ifndef FIELDS_H__
#define FIELDS_H__

#include <sys/types.h>

#include "maybe.H"
#include "tid.H"
#include "tmpheap.H"

namespace fields {

class fieldbuf {
    friend void flush();
    struct fragment : tmpheap::base {
        struct fragment *next;
        unsigned short used;
        char content[4086];
    };
    fragment *head;
    fragment *tail;
public:
    fieldbuf();
    char *c_str(maybe<unsigned> limit = Nothing);
    void push(const char *);
};

class field : protected tmpheap::base {
    void operator=(const field &) = delete;
protected:
    field(const field &) {}
    field();
public:
    virtual void fmt(fieldbuf &) const = 0;
    virtual const field &escape() const;
    /* Returns a temporary heap string */
    const char *c_str() const;
};

extern const field &space;
extern const field &comma;
extern const field &period;

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

class strfield : public field {
private: char *const content;
private: bool const escape_;
private: strfield(const char *, bool);
public:  static const strfield &n(const char *);
public:  const strfield &escape() const final;
public:  void fmt(fieldbuf &) const;
};
const strfield &mk(const char *);
const field &operator+(const char *, const field &);
const field &operator+(const field &, const char *);

class intfield : public field {
    friend const intfield &mk(long);
    friend const intfield &mk(unsigned long);
    const unsigned long val_;
    const unsigned base_;
    const field &sep_;
    const unsigned sepwidth_;
    const bool uppercase_;
    const bool alwayssign_;
    const bool hidebase_;
    /* true if val_ is really a signed long */
    const bool signed_;
    intfield(const intfield &o) = delete;
    intfield(unsigned long _val, unsigned _base, const field &sep,
             unsigned sepwidth, bool _uppercase, bool _alwayssign,
             bool _hidebase, bool signed_);
    static const intfield &n(unsigned long val, unsigned base, const field &sep,
                             unsigned sepwidth, bool uppercase,
                             bool alwayssign, bool hidebase,
                             bool signed_);
public:
    const intfield &base(unsigned b) const;
    const intfield &nosep() const;
    const intfield &sep(const field & = comma, unsigned = 3) const;
    const intfield &uppercase() const;
    const intfield &alwayssign() const;
    const intfield &hidebase() const;
    void fmt(fieldbuf &) const;
};
const intfield &mk(long);
const intfield &mk(unsigned long);
static inline const intfield &mk(int x) { return mk((long)x); }
static inline const intfield &mk(unsigned x) { return mk((unsigned long)x); }

class doublefield : public field {
    long double val_;
    doublefield(long double _val);
    doublefield(const doublefield &o) = delete;
public:
    static const doublefield &n(long double val);
    void fmt(fieldbuf &) const;
};
const doublefield &mk_double(long double);

void print(const field &f);

template <typename t> const field &mk(const ::maybe<t> &m);

};

#endif /* !FIELDS_H__ */
