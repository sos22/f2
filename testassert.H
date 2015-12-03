/* A little thing which tries to make it easy to do informative
 * assert()s in test programs, by lifting things up to be lazy and to
 * log components of the assertion e.g. if you go
 *
 * tassert(T(x) < T(y) + T(z))
 *
 * and it fails the error message will look like this:
 *
 * assert failed at foo.C:foofunc:73: !(x{5} < y{1} + z{2})
 *
 * If it's something like:
 *
 * tassert(T(x) && T(y));
 *
 * And x evaluates to false then y won't be evaluated at all and the
 * error will look like this:
 *
 * assert failed at foo.C:foofunc:73: !(x{false} && y{...});
 */
#ifndef TESTASSERT_H__
#define TESTASSERT_H__

#include "fields.H"
#include "logging.H"

namespace testassert {
template <typename inner> class expression {
private: void operator=(expression &) = delete;
public:  virtual const fields::field &field(
    unsigned prec, bool isleft, const char *parentop) const = 0;
public:  const fields::field &field() const { return field(0, true, NULL); }
public:  virtual inner eval() = 0;
public:  virtual ~expression(); };

template <typename inner> class val : public expression<inner> {
private: val() = delete;
private: void operator=(val &) = delete;
public:  ~val();
public:  const char *label;
public:  std::function<inner ()> calc;
public:  maybe<inner> value;
public:  val(const char *_label, const std::function<inner ()> &_calc);
public:  const fields::field &field(unsigned, bool, const char *) const;
public:  inner eval(); };

template <typename inner> class cnst : public expression<inner> {
private: cnst() = delete;
private: void operator=(cnst &) = delete;
public:  ~cnst();
public:  const inner value;
public:  cnst(inner _value);
public:  const fields::field &field(unsigned, bool, const char *) const;
public:  inner eval(); };

#define binop(op)                                                       \
    template <typename a, typename b>                                   \
    expression<decltype(*(a *)1ul op *(b *)2ul)> &operator op(          \
        expression<a> &,                                                \
        expression<b> &)
binop(+);
binop(*);
binop(-);
binop(&&);
binop(||);
binop(>);
binop(>=);
binop(==);
binop(<);
binop(<=);
#undef binop

template <> class cnst<_Nothing> : public expression<_Nothing> {
private: cnst() = delete;
private: void operator=(const cnst &) = delete;
public:  cnst(_Nothing);
public:  const fields::field &field(unsigned, bool, const char *) const;
public:  _Nothing eval(); };
template <> class val<_Nothing> : public expression<_Nothing> {
private: val() = delete;
private: void operator=(const val &) = delete;
public:  val(const char *, const std::function<_Nothing ()> &);
public:  const fields::field &field(unsigned, bool, const char *) const;
public:  _Nothing eval(); };
}

template <typename t> t __remove_referenceness__(const t &x) { return x; }

/* T for when the expression cannot be used in decltype e.g. it
 * contains a lambda. */
#define T2(type, f, ...)                                                \
    (*(__builtin_constant_p(f##__VA_ARGS__)                             \
       ? static_cast<testassert::expression<type> *>(                   \
           new testassert::cnst<type>(                                  \
               f##__VA_ARGS__))                                         \
       : static_cast<testassert::expression<type> *>(                   \
           new testassert::val<type>(                                   \
               #f #__VA_ARGS__ ,                                        \
               [&] { return f##__VA_ARGS__ ; } ))))

#define T(...) T2(decltype(__remove_referenceness__(__VA_ARGS__)), __VA_ARGS__)

#define tassert(...)                                                    \
    do {                                                                \
        testassert::expression<bool> &__e(__VA_ARGS__);                 \
        if (!__e.eval()) {                                              \
            logmsg(loglevel::emergency,                                 \
                   "assertion failed: " __FILE__                        \
                   ":" + fields::mk(__func__) +                         \
                   ":" + fields::mk(__LINE__) +                         \
                   ": " + __e.field());                                 \
            abort(); }                                                  \
        delete &__e; }                                                   \
    while (0)

#endif /* !TESTASSERT_H__ */
