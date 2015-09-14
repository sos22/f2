#ifndef ORERROR_H__
#define ORERROR_H__

#include "either.H"
#include "error.H"

template <typename> class parser;

namespace fields { class field; }

/* Tag types used purely to allow constructor disambiguation. */
class _Success { };
extern const _Success Success;
class _Failure { };
extern const _Failure Failure;

template <typename t> class orerror;
template <> class orerror<void>;
template <> class orerror<error>;

template <typename t> class orerror : public either<error, t> {
public: orerror(error e) : either<error, t>(Left(), e) {}
public: orerror(const t &x) : either<error, t>(Right(), x) {}
public: orerror(_Steal s, t &x) : either<error, t>(s, Right(), x) {}
public: orerror(_Steal s, orerror &o) : either<error, t>(s, o) {}
public: explicit orerror(deserialise1 &ds) : either<error, t>(ds) {}
public: template <typename ... args> orerror(_Success, args &&...params)
    : either<error, t>(Right(), std::forward<args>(params)...) {}
public: orerror(_Failure, error e) : either<error, t>(Left(), e) {}
public: bool operator==(const error &e) const {
    return this->isleft() && this->left() == e; }
public: bool operator==(const t &o) const {
    return this->isright() && this->right() == o; }
public: bool operator==(const orerror &o) const {
    return
        (this->isleft() && o.isleft() && this->left() == o.left()) ||
        (this->isright() && o.isright() && this->right() == o.right()); }
public: void operator=(error e) { this->mkleft(e); }
public: void operator=(const t &x) { this->mkright(x); }
public: void operator=(const orerror &o) {
    if (o.isleft()) this->mkleft(o.left());
    else this->mkright(o.right()); }
public: error failure() const {
    assert(this->isfailure());
    return this->left(); }
public: const t &fatal(const fields::field &f) const {
    if (this->isfailure()) this->failure().fatal(f);
    else return this->success(); }
public: const t &fatal(const char *f) const {
    if (this->isfailure()) this->failure().fatal(f);
    else return this->success(); }
public: const fields::field &field() const;
public: void mkfailure(error e) { this->mkleft(e); }
public: template <typename ... args> void mksuccess(args &&...params) {
    this->mkright(std::forward<args>(params)...); }
public: bool isfailure() const { return this->isleft(); }
public: bool issuccess() const { return this->isright(); }
public: const t &success() const {
    assert(this->issuccess());
    return this->right(); }
public: t &success() {
    assert(this->issuccess());
    return this->right(); }
public: void warn(const fields::field &f) const {
    if (this->isfailure()) this->failure().warn(f); }
public: void warn(const char *f) const {
    if (this->isfailure()) this->failure().warn(f); }
public: static const ::parser<orerror<t> > &parser();
public: static const ::parser<orerror<t> > &parser(const ::parser<t> &inner);
public: const t flatten() const; };

template <typename t> orerror<t>
success(const t &x) { return x; }
template <typename t> bool
operator==(error a, const orerror<t> &b) { return b == a; }
template <typename t> bool
operator==(const t &a, const orerror<t> &b) { return b == a; }

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
