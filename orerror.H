#ifndef ORERROR_H__
#define ORERROR_H__

#include <assert.h>

#include <utility>

#include "error.H"
#include "maybe.H"

class serialise1;
class deserialise1;

/* Tag type used purely to allow constructor disambiguation. */
class _Success { };
extern const _Success Success;

template <typename t> class orerror {
    bool errored;
    union {
        error err;
        t inner;
    };
    orerror() = delete;
public:
    explicit orerror(deserialise1 &ds)
        : errored(ds) {
        if (errored) new (&err) error(ds);
        else new (&inner) t(ds); }
    orerror(const t &v) : errored(false) { new (&inner) t(v); }
    orerror(const orerror &o)
        : errored(o.errored) {
        if (errored) new (&err) error(o.err);
        else new (&inner) t(o.inner); }
    orerror(t &&v) : errored(false) { new (&inner) t(std::move(v)); }
    orerror(const error &_err) : errored(true), err(_err) { }
    template <typename ... args> orerror(_Success, args &&...params)
        : errored(false),
          inner(std::forward<args>(params)...) {}
    void operator=(const orerror &o) {
        if (errored) {
            if (o.errored) err = o.err;
            else {
                err.~error();
                errored = false;
                new (&inner) t(o.inner); }
        } else {
            if (o.errored) {
                inner.~t();
                errored = true;
                new (&inner) error(o.err); }
            else inner = o.inner; } }
    ~orerror() {
        if (errored) err.~error();
        else inner.~t(); }
    const t &success() const {
        assert(!errored);
        return inner; }
    t &success() {
        assert(!errored);
        return inner; }
    const error &failure() const {
        assert(errored);
        return err; }
    bool issuccess() const { return !errored; }
    bool isfailure() const { return errored;  }

    const t &fatal(const fields::field &w) const {
        if (errored) err.fatal(w);
        else return inner; }
    const t &fatal(const char *w) const {
        if (errored) err.fatal(w);
        else return inner; }

    const t dflt(const t &o) const {
        if (errored) return o;
        else return inner; }

    template <typename o> orerror(const orerror<o> &other)
        : errored(other.errored) {
        if (errored) new (&err) error(other.err);
        else new (&inner) t(other.inner); }

    template <typename o> orerror<o> map(std::function<o (const t &)> f) {
        if (errored) return err;
        else return f(inner); }

    bool operator==(const error &e) const { return errored && err == e; }
    bool operator==(const t &e) const { return !errored && inner == e; }
    bool operator!=(const error &e) const { return !(*this == e); }
    bool operator!=(const t &e) const { return !(*this == e); }

    void serialise(serialise1 &s) const;
};

template <typename t> bool
operator==(error a, const orerror<t> &b) { return b == a; }
template <typename t> bool
operator==(const t &a, const orerror<t> &b) { return b == a; }
template <typename t> bool
operator!=(error a, const orerror<t> &b) { return b != a; }
template <typename t> bool
operator!=(const t &a, const orerror<t> &b) { return b != a; }

/* Special case for things which just return an error indication.
   This gives a slightly nicer API than maybe<error> (or at least a
   more consistent one). */
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

namespace tests { void _orerror(); }

#endif /* !ORERROR_H__ */
