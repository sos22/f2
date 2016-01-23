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
#include "void.H"

class crashcontext;
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

/* Tag type used to select the right constructor overload */
class Just {};

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
    maybe(const maybe &o);
    ~maybe();
    maybe(_Nothing);
    maybe(const t &what);
    
    template <typename ... args> maybe(Just, args&&...params);
    
    /* Convert @this into a just<t> by invoking the <t> constructor,
     * using @params as the constructor arguments.  If @this is
     * already a just<t> then the t destructor is invoked to clear it
     * out. */
    template <typename ... args> void mkjust(args &&... params);
    /* Convert @this to a Nothing, if it isn't already one, by
     * invoking the t destructor.  This is occasionally more
     * convenient than using *@this = Nothing if the t type doesn't
     * have an assignment operator. */
    void mknothing();
    
    /* Assignment from one maybe to another, using the constructor to
     * convert Nothing to Just, the destructor to convert Just to
     * Nothing, and operator= to convert one Just to another.  We
     * could equally use the constructor and destructor to convery
     * between Justs.  Doing it this way means that you can't use
     * operator= on maybe<t> unless you can use operator= on t, which
     * seems like a good restriction to have. */
    void operator=(const maybe &o);
    void operator=(_Nothing);
    void operator=(const t &o);
    bool isnothing() const;
    bool operator!() const;
    bool isjust() const;
    const t &just() const;
    t &just();
    t just(_Steal);
    /* Default value.  x.dflt(y) is y if x is Nothing and x.just()
     * otherwise. */
    const t &dflt(const t &what) const;
    /* Error handling: If x is Nothing, x.fatal(msg) terminates the
     * program and prints the message msg.  Otherwise, x.fatal(msg) is
     * x.just(). */
    const t &fatal(const fields::field &msg) const;
    const t &fatal(const char *msg) const;
    bool operator==(const maybe &o) const;
    bool operator!=(const maybe &o) const;
    bool operator==(_Nothing) const;
    bool operator!=(_Nothing) const;
    bool operator==(const t &o) const;
    bool operator!=(const t &o) const;

    /* Lift a function into maybe<t>.  x.map(f) is Nothing if x is
     * Nothing and is otherwise mkjust(f(x.just())). */
    template <typename o> maybe<o> map(const std::function<o (const t&)> &f);

    void serialise(serialise1 &) const;

    /* Extract the contents of the maybe, without first checking
     * whether the maybe is non-Nothing, and without dereferencing
     * @this at all.  This is only really useful for containerof. */
    const t &__just() const;
    
    /* Suppress maybe-uninitialised compiler warnings on content by
     * pretending to initialise it.  These warnings should always be
     * spurious, so this should always be safe; it's purely for
     * working around compiler weaknesses. */
    /* It's also quite expensive for types with non-trivial
     * constructors.  Fortunately, gcc only seems to generate the bad
     * warning for primitive types, so we should be okay. */
    template <typename ... args> void silencecompiler(args &&...params);
    
    const fields::field &field() const;
    const fields::field &field(crashcontext) const;
};

template <typename t> maybe<t> mkjust(const t &x);

/* Special case for maybe<void>.  Semantically, this is just a bool;
 * its only purpose is to make other templates come out a little bit
 * more cleanly. */
template <> class maybe<void> : public maybe<Void> {
private: maybe() : maybe<Void>(Void()) {}
public:  maybe(_Nothing n) : maybe<Void>(n) {}
public:  static maybe just; };

template <typename t> bool operator ==(const t &a, const maybe<t> &o);

template <typename t> bool operator !=(const t &a, const maybe<t> &o);

template <typename> class parser;
namespace parsers {
template <typename t> const parser<maybe<t> > &_maybe(const parser<t> &);
}

#endif /* !MAYBE_H__ */
