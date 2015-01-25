#ifndef ORERROR_H__
#define ORERROR_H__

#include <assert.h>

#include <utility>

#include "error.H"
#include "maybe.H"

class serialise1;
class deserialise1;

template <typename t> class orerror {
    bool errored;
    union {
        error err;
        char buf[sizeof(t)];
    };
    orerror() = delete;
public:
    explicit orerror(deserialise1 &ds)
        : errored(ds) {
        if (errored) new (&err) error(ds);
        else new (buf) t(ds); }
    orerror(const t &v)
        : errored(false)
        {
            new (this->buf) t(v);
        }
    orerror(const orerror &o)
        : errored(o.errored) {
        if (errored) new (&err) error(o.err);
        else new (buf) t(*(t *)o.buf); }
    orerror(t &&v)
        : errored(false)
        {
            new (this->buf) t(std::move(v));
        }
    orerror(const error &_err)
        : errored(true), err(_err)
        {
        }
    void operator=(const orerror &o) {
        if (isfailure()) {
            if (o.isfailure()) {
                err = o.failure();
            } else {
                failure().~error();
                errored = false;
                new (this->buf) t(o.success()); }
        } else {
            if (o.isfailure()) {
                success().~t();
                errored = true;
                new (&this->err) error(o.failure());
            } else {
                success() = o.success(); } } }
    ~orerror() {
        if (errored) failure().~error();
        else success().~t(); }
    const t &success() const {
        assert(!errored);
        return *(const t *)buf;
    }
    t &success() {
        assert(!errored);
        return *(t *)buf;
    }
    const error &failure() const {
        assert(errored);
        return err;
    }
    bool issuccess() const {
        return !errored;
    }
    bool isfailure() const {
        return errored;
    }

    const t &fatal(const fields::field &w) const {
        if (isfailure()) failure().fatal(w);
        else return success(); }
    const t &fatal(const char *w) const {
        if (isfailure()) failure().fatal(w);
        else return success(); }

    const t dflt(const t &o) const {
        if (issuccess()) return success();
        else return o; }

    template <typename o> orerror(const orerror<o> &other)
        : errored(other.isfailure()) {
        if (errored) {
            new (&err) error(other.failure());
        } else {
            new (buf) t(other.success()); } }

    template <typename o> orerror<o> map(std::function<o (const t &)> f) {
        if (isfailure()) return failure();
        else return f(success()); }

    bool operator==(const error &e) const {
        return isfailure() && failure() == e; }
    bool operator==(const t &e) const {
        return issuccess() && success() == e; }
    bool operator!=(const error &e) const {
        return !(*this == e); }
    bool operator!=(const t &e) const {
        return !(*this == e); }

    void serialise(serialise1 &s) const;
};

template <typename t> bool
operator==(const t &a, const orerror<t> &b) {
    return b.issuccess() && a == b.success(); }

/* Special case for things which just return an error indication.
   This gives a slightly nicer API than maybe<error> (or at least a
   more consistent one). */
class _Success {
};
extern const _Success Success;
template <> class orerror<void> {
private: maybe<error> content;
public:  orerror(deserialise1 &ds)
    : content(ds) {}
public:  orerror(_Success) : content(Nothing) {}
public:  orerror(const error &e) : content(e) {}
public:  template <typename t> orerror(const orerror<t> &o)
    : content(Nothing) {
    if (o.isfailure()) content.mkjust(o.failure()); }
public:  void operator=(const orerror<void> &e) {
    content = e.content; }
public:  bool issuccess() const { return content.isnothing(); }
public:  bool isfailure() const { return content.isjust(); }
public:  error failure() const { return content.just(); }
public:  void fatal(const fields::field &f) const {
    if (isfailure()) failure().fatal(f); }
public:  void warn(const fields::field &f) const {
    if (isfailure()) failure().warn(f); }
public:  void fatal(const char *f) const {
    if (isfailure()) failure().fatal(f); }
public:  void warn(const char *f) const {
    if (isfailure()) failure().warn(f); }
public:  orerror<void> iffailed(const std::function<void ()> &f) {
    if (isfailure()) f();
    return *this; }
public:  bool operator==(error e) const { return content == e; }
public:  bool operator!=(error e) const { return content != e; }
public:  bool operator==(_Success) const { return issuccess(); }
public:  bool operator!=(_Success) const { return !issuccess(); }
public:  void serialise(serialise1 &s) const { content.serialise(s); }
};

template <typename t> orerror<t>
success(const t &x) { return x; }

#endif /* !ORERROR_H__ */
