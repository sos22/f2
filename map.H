/* Simple key to value mapping.  Keys have no defined order within the
 * map.  Each key must have precisely one value. */
/* This implementatation is basically an a-list, so not much more
 * performant than a list<pair<key, value> >.  The big advantages are
 * (a) documenting the intended semantics of the list, (b) a very
 * slightly nicer API, and (c) it gives us somewhere to hook a better
 * implementation later. */
#ifndef MAP_H__
#define MAP_H__

#include "empty.H"
#include "list.H"
#include "pair.H"
#include "parsers.H"

class deserialise1;
class serialise1;
template <typename> class maybe;
template <typename> class parser;
class quickcheck;

namespace fields { class field; }

namespace tests { void _map(); }

template <typename key> unsigned long default_hashfn(const key &);
template <> unsigned long default_hashfn<int>(const int &);
template <> unsigned long default_hashfn<unsigned int>(const unsigned int &);
template <> unsigned long default_hashfn<unsigned long>(const unsigned long &);
template <> unsigned long default_hashfn<const char *>(const char *const &);
template <> unsigned long default_hashfn<char *>(char *const &);

template <typename _key,
          typename _value,
          unsigned long hash(const _key &) = default_hashfn> class testmap;
template <typename _key,
          typename _value,
          unsigned long hash(const _key &) = default_hashfn> class map {
    friend class testmap<_key, _value, hash>;
    template <typename k, typename, unsigned long h(const k &)>
        friend class map;
private: class entry {
        /* next and h fields must be before anything which depends on
         * key or value parameters, for rehash() */
    public: entry *next;
    public: unsigned long h;
    public: _key k;
    public: _value v;
    public: template <typename ... args> entry(
        unsigned long _h,
        const _key &_k,
        args && ... params)
        : next(NULL), h(_h), k(_k), v(std::forward<args>(params)...) {} };
    /* It's important that these fields don't themselves use the key or
     * value parameters so that rehash() works. */
private: unsigned nrbuckets;
private: unsigned nritems;
private: entry **buckets;
private: static bool isprime(unsigned);
private: static unsigned nextsize(unsigned);
private: static unsigned prevsize(unsigned);
private: void rehash(unsigned);
    /* Serialise/deserialise interface. */
public:  map(deserialise1 &ds);
public:  void serialise(serialise1 &s) const;
    /* Construct a random map. */
public:  map(quickcheck &);
    /* Construct an empty map. */
public:  map(Empty = empty) : nrbuckets(0), nritems(0), buckets(NULL) {}
    /* Construct a map from an immediate list of items. */
public:  template <typename ... args> map(const _key &k,
                                          const _value &v,
                                          args &&... params)
    : map(std::forward<args>(params)...) { set(k, v); }
    /* Constuct a map by stealing an existing one's content.  The
     * existing map becomes empty afterwards. */
public:  map(_Steal, map &what)
    : nrbuckets(what.nrbuckets),
      nritems(what.nritems),
      buckets(what.buckets) {
    what.nrbuckets = 0;
    what.nritems = 0;
    what.buckets = NULL; }
    /* Construct a map by copying an existing one. */
public: map(const map &what);
    /* Default destructor. */
public: ~map();
    /* Set an entry in the map.  It must not already be set. */
public:  template <typename...args> _value &set(const _key &, args &&...params);
    /* Get an entry in the map, or Nothing if it isn't set. */
public:  maybe<_value> get(const _key &) const;
    /* true if the key is present in the map and false
     * otherwise. Equivalent to get() != Nothing, but a little bit
     * faster if the value has a non-trivial copy constructor. */
public:  bool haskey(const _key &) const;
    /* Get an entry in the map, or NULL if it isn't set. */
public:  _value *get_mutate(const _key &);
    /* Clear an entry in the map.  It must be set. */
public:  void clear(const _key &);
    /* Clear every entry in the map. */
public:  void flush();
    /* Equality tests. */
public:  bool operator==(const map &) const;
    /* Iterator interface. */
public:  class iterator {
        friend class map;
    private: map *owner;
    private: unsigned bucketnr;
    private: entry *cursor;
    private: explicit iterator(map *_owner);
    public:  void next();
    public:  void remove();
    public:  bool finished() const;
    public:  const _key &key() const { return cursor->k; }
    public:  const _value &value() const { return cursor->v; }
    public:  _value &value() { return cursor->v; } };
public:  iterator start() { return iterator(this); }
public:  class constiterator {
        friend class map;
    private: iterator it;
    private: constiterator(map *_owner) : it(_owner) {}
    public:  void next() { it.next(); }
    public:  bool finished() const { return it.finished(); }
    public:  const _key &key() const { return it.key(); }
    public:  const _value &value() const { return it.value(); } };
public:  constiterator start() const {
    return constiterator(const_cast<map *>(this)); }
    /* Field interface. */
public:  class fieldimpl;
public:  const fieldimpl &field() const;
    /* Parser interface. */
public:  class parserimpl;
public:  static const parserimpl &parser(
    const parser<_key> & = parsers::defaultparser<_key>(),
    const parser<_value> & = parsers::defaultparser<_value>()); };

template <> bool map<int, int, default_hashfn>::isprime(unsigned );
template <> void map<int, int, default_hashfn>::rehash(unsigned);
template <> unsigned map<int, int, default_hashfn>::nextsize(unsigned);
template <> unsigned map<int, int, default_hashfn>::prevsize(unsigned);

#endif /* !MAP_H__ */
