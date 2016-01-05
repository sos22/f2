#ifndef ORERROR_H__
#define ORERROR_H__

#include "either.H"
#include "error.H"

template <typename> class parser;

namespace fields { class field; }

/* Tag types used purely to allow constructor disambiguation. */
class _Success {
public: const fields::field &field() const; };
extern const _Success Success;
class _Failure { };
extern const _Failure Failure;

template <typename t> class orerror;
template <> class orerror<void>;
template <> class orerror<error>;

template <typename t> class orerror : public either<error, t> {
public: orerror(error e);
public: orerror(const t &x);
public: orerror(_Steal s, t &x);
public: orerror(_Steal s, orerror &o);
public: explicit orerror(deserialise1 &ds);
public: template <typename ... args> orerror(_Success, args &&...params);
public: orerror(_Failure, error e);
public: bool operator==(const error &e) const;
public: bool operator==(const t &o) const;
public: bool operator==(const orerror &o) const;
public: void operator=(error e) { this->mkleft(e); }
public: void operator=(const t &x) { this->mkright(x); }
public: void operator=(const orerror &o);
public: error failure() const;
public: const t &fatal(const fields::field &f) const;
public: const t &fatal(const char *f) const;
public: const fields::field &field() const;
public: void mkfailure(error e);
public: template <typename ... args> void mksuccess(args &&...params);
public: bool isfailure() const;
public: bool issuccess() const;
public: const t &success() const;
public: t &success();
public: const orerror &warn(const fields::field &f) const;
public: const orerror &warn(const char *f) const;
public: static const ::parser<orerror<t> > &parser();
public: static const ::parser<orerror<t> > &parser(const ::parser<t> &inner);
public: const t flatten() const; };

template <typename t> orerror<t> success(const t &x);
template <typename t> bool operator==(error a, const orerror<t> &b);
template <typename t> bool operator==(const t &a, const orerror<t> &b);

template <> class orerror<void> : public orerror<Void> {
public: using orerror<Void>::orerror;
public: template <typename t> orerror(const orerror<t> &o)
    : orerror<Void>(error::unknown) {
    if (o.isfailure()) this->mkfailure(o.failure());
    else this->mksuccess(); } };

/* orerror<error> produces a lot of ambiguous overloads. Use an
 * explicit template specialisation to allow us to use the safe
 * interfaces without getting errors from the dodgy ones. */
template <> class orerror<error> {
private: bool _succ;
private: error _err;
public:  orerror(_Failure, error e) : _succ(false), _err(e) {}
public:  orerror(_Success, error e) : _succ(true), _err(e) {}
public:  bool issuccess() const { return _succ; }
public:  bool isfailure() const { return !_succ; }
public:  const error &fatal(const fields::field &f) const {
    if (this->isfailure()) this->failure().fatal(f);
    else return this->success(); }
public:  const error &success() const {
    assert(_succ);
    return _err; }
public:  const error &failure() const {
    assert(!_succ);
    return _err; } };

#endif /* !ORERROR_H__ */
