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
    void reset();
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
public:  const strfield &escape() const;
public:  void fmt(fieldbuf &) const;
};
const strfield &mk(const char *);
const field &operator+(const char *, const field &);
const field &operator+(const field &, const char *);

class intfield : public field {
    long val_;
    int base_;
    const field &sep_;
    unsigned sepwidth_;
    bool uppercase_;
    bool alwayssign_;
    bool hidebase_;
    intfield(const intfield &o) = delete;
    intfield(long _val, int _base, const field &sep, unsigned sepwidth,
             bool _uppercase, bool _alwayssign, bool _hidebase);
public:
    static const intfield &n(long val, int base, const field &sep,
                             unsigned sepwidth, bool uppercase,
                             bool alwayssign, bool hidebase);
    const intfield &base(int b) const;
    const intfield &nosep() const;
    const intfield &sep(const field & = comma, unsigned = 3) const;
    const intfield &uppercase() const;
    const intfield &alwayssign() const;
    const intfield &hidebase() const;
    void fmt(fieldbuf &) const;
};
const intfield &mk(long);

class doublefield : public field {
    double val_;
    doublefield(double _val);
    doublefield(const doublefield &o) = delete;
public:
    static const doublefield &n(double val);
    void fmt(fieldbuf &) const;
};
const doublefield &mk_double(double);

void print(const field &f);

template <typename t> const field &mk(const ::maybe<t> &m);

};

namespace tests {
void fields();
}

#endif /* !FIELDS_H__ */
