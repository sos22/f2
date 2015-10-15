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

namespace testassert {
template <typename inner> class expression {
public: virtual const fields::field &field() const = 0;
public: virtual inner eval() = 0;
public: virtual ~expression(); };

template <typename inner> class val : public expression<inner> {
public: const char *label;
public: std::function<inner ()> calc;
public: maybe<inner> value;
public: val(const char *_label, const std::function<inner ()> &_calc);
public: const fields::field &field() const;
public: inner eval(); };

template <typename inner> class cnst : public expression<inner> {
public: const inner value;
public: cnst(inner _value);
public: const fields::field &field() const;
public: inner eval(); };

#define binop(op)                                                       \
    template <typename a, typename b>                                   \
    expression<decltype(*(a *)1ul op *(b *)2ul)> &operator op(          \
        expression<a> &,                                                \
        expression<b> &)
binop(+);
binop(-);
binop(&&);
binop(||);
binop(>);
binop(==);
binop(<);
#undef binop

}

#define T(f, ...)                                                       \
    *(__builtin_constant_p(f##__VA_ARGS__)                              \
      ? static_cast<testassert::expression<decltype(f##__VA_ARGS__)> *>( \
          new testassert::cnst<decltype(f##__VA_ARGS__)>(               \
              f##__VA_ARGS__))                                          \
      : static_cast<testassert::expression<decltype(f##__VA_ARGS__)> *>( \
          new testassert::val<decltype(f##__VA_ARGS__)>(                \
              #f #__VA_ARGS__ ,                                         \
              [&] { return f##__VA_ARGS__ ; } )))

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
