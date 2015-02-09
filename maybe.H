/* Template to extend a type with a designated bottom element.  An
 * instance of maybe<t> is either a T or the special Nothing element.
 * This is often handy for handling optional arguments and for some
 * kinds of simple failure e.g. trying to pop an entry from an empty
 * queue.  More complicated failures are better handled by the orerror
 * template instead.  The big advantage is uniformity: an optional T
 * is always represented by maybe<T>, regardless of what T is, so, for
 * instance, if you want to optionally limit the number of things in a
 * queue, you give the limit the type maybe<unsigned>, rather than
 * needing to define that the a limit of zero is special somehow.  The
 * template invokes t's constructor, destructor, operator=, and
 * operator== at the obvious times to make things work in the obvious
 * ways. */
#ifndef MAYBE_H__
#define MAYBE_H__

#include <algorithm>
#include <functional>

#include <assert.h>
#include <string.h>

#include "error.H"

class deserialise1;
class quickcheck;
class serialise1;

template <typename> class maybe;

/* The Nothing element, shared amonhst all of the maybe types.  This
 * doesn't carry any data and is never actually used at run-time; it's
 * just here to drive the static operator and constructor overloading
 * bits. */
class _Nothing {
public: template <typename t> bool operator==(const maybe<t> &) const;
public: template <typename t> bool operator!=(const maybe<t> &) const;
public: _Nothing() {}
};
const class _Nothing Nothing;

template <typename t>
class maybe {
    bool _isnothing;
    /* Hide it in a union so that we don't need to construct it when
     * we don't want to. */
    union {
        t content;
    };
    maybe() = delete;
public:
    explicit maybe(deserialise1 &);
    explicit maybe(quickcheck &);
    maybe(const maybe &o)
        : _isnothing(o._isnothing) {
        if (!_isnothing) new (&content) t(o.just()); }
    /* Move constructor for moves from other maybe<t>s.  Note that
     * this leaves @o as a just<t>, rather than converting it to
     * Nothing, and does a move on the inner t. */
    maybe(maybe &&o)
        : _isnothing(o._isnothing) {
        if (!_isnothing) new (&content) t(std::move(o.content)); }
    ~maybe() { if (isjust()) content.~t(); }
    maybe(_Nothing) : _isnothing(true) {}
    maybe(const t &what) : _isnothing(false) { ::new (&content) t(what); }
    maybe(t &&what) : _isnothing(false) { ::new (&content) t(what); }

    /* Convert @this into a just<t> by invoking the <t> constructor,
     * using @params as the constructor arguments.  If @this is
     * already a just<t> then the t destructor is invoked to clear it
     * out. */
    template <typename ... args> void mkjust(args &&... params);
    /* Convert @this to a Nothing, if it isn't already one, by
     * invoking the t destructor.  This is occasionally more
     * convenient than using *@this = Nothing if the t type doesn't
     * have an assignment operator. */
    void mknothing() {
        if (!_isnothing) content.~t();
        _isnothing = true; }

    /* Assignment from one maybe to another, using the constructor to
     * convert Nothing to Just, the destructor to convert Just to
     * Nothing, and operator= to convert one Just to another.  We
     * could equally use the constructor and destructor to convery
     * between Justs.  Doing it this way means that you can't use
     * operator= on maybe<t> unless you can use operator= on t, which
     * seems like a good restriction to have. */
    void operator=(const maybe &o) {
        if (_isnothing) {
            if (o._isnothing) return;
            else {
                new (&content) t(o.content);
                _isnothing = false; } }
        else {
            if (o._isnothing) {
                content.~t();
                _isnothing = true; }
            else content = o.content; } }
    void operator=(_Nothing) {
        if (!_isnothing) content.~t();
        _isnothing = true; }
    void operator=(const t &o) {
        if (_isnothing) {
            new (&content) t(o);
            _isnothing = false; }
        else content = o; }
    bool isnothing() const { return _isnothing; }
    bool operator!() const { return isnothing(); }
    bool isjust() const { return !isnothing(); }
    const t &just() const {
        assert(isjust());
        return content; }
    t &just() {
        assert(isjust());
        return content; }
    /* Default value.  x.dflt(y) is y if x is Nothing and x.just()
     * otherwise. */
    const t &dflt(const t &what) const {
        if (isjust()) return content;
        else return what; }
    /* Error handling: If x is Nothing, x.fatal(msg) terminates the
     * program and prints the message msg.  Otherwise, x.fatal(msg) is
     * x.just(). */
#ifndef COVERAGESKIP
    const t &fatal(const fields::field &msg) const {
        if (isjust()) return just();
        else error::nothing.fatal(msg); }
    const t &fatal(const char *msg) const {
        if (isjust()) return just();
        else error::nothing.fatal(msg); }
#endif
    bool operator==(const maybe &o) const {
        if (_isnothing != o._isnothing) return false;
        else if (_isnothing) return true;
        else return content == o.content; }
    bool operator!=(const maybe &o) const {
        if (_isnothing != o._isnothing) return true;
        else if (_isnothing) return false;
        else return content != o.content; }
    bool operator==(const _Nothing) const { return _isnothing; }
    bool operator!=(const _Nothing) const { return !_isnothing; }
    bool operator==(const t &o) const { return !_isnothing && content == o; }
    bool operator!=(const t &o) const { return _isnothing || content != o; }

    /* Lift a function into maybe<t>.  x.map(f) is Nothing if x is
     * Nothing and is otherwise mkjust(f(x.just())). */
    template <typename o> maybe<o> map(const std::function<o (const t&)> &f) {
        if (isnothing()) return Nothing;
        else return f(just()); }

    void serialise(serialise1 &) const;

    const fields::field &field() const;
};

/* Special case for maybe<void>.  Semantically, this is just a bool;
 * its only purpose is to make other templates come out a little bit
 * more cleanly. */
template <> class maybe<void> {
private: bool r;
private: maybe() : r(false) {}
public:  maybe(const quickcheck &);
public:  template <typename t> maybe(const maybe<t> &o)
    : r(o.r) {}
public:  maybe(_Nothing)
    : r(true) {}
public:  static maybe just;
public:  void operator=(const maybe &o) {
    r = o.r; }
public:   bool isnothing() const { return r; }
public:   bool operator!() const { return isnothing(); }
public:   bool isjust() const { return !r; }
public:   bool operator==(const _Nothing) const {
    return isnothing(); }
public:   bool operator!=(const _Nothing) const {
    return isjust(); }
public:   bool operator==(const maybe<void> &x) const {
    return isjust() == x.isjust(); }
public:   bool operator!=(const maybe<void> &x) const {
    return !(*this == x); }
public:   template <typename o> maybe<o> map(const std::function<o ()> &f) {
        if (isnothing()) return Nothing;
        else return f(); } };

template <typename t> bool
_Nothing::operator==(const maybe<t> &o) const { return o.isnothing(); }

template <typename t> bool
_Nothing::operator!=(const maybe<t> &o) const { return o.isjust(); }

template <typename t> bool
operator ==(const t &a, const maybe<t> &o) {
    return o.isjust() && a == o.just(); }

template <typename t> bool
operator !=(const t &a, const maybe<t> &o) {
    return o.isnothing() || a != o.just(); }

template <typename> class parser;
namespace parsers {
template <typename t> const parser<maybe<t> > &_maybe(const parser<t> &);
}

namespace tests {
void _maybe();
}

#endif /* !MAYBE_H__ */
