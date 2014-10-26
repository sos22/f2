/* Simple scheme for serialising and deserialising types into buffers.
 * Tries to be a bit less of a pain than the wireproto mechanism.
 * Each version of the protocol defines serialise and deserialise
 * contexts and we use inheritance and type conversion operators to
 * provide the desired inclusion rules.  The deserialise operations
 * are broadly failure oblivious: if something fails then you still
 * get something back, but we set a flag saying that something's gone
 * wrong which you're supposed to check before using the results. */
#ifndef SERIALISE_H__
#define SERIALISE_H__

#include "either.H"
#include "nnp.H"
#include "orerror.H"

class buffer;
class quickcheck;

/* Caution: this consumes part of the buffer even when it fails. */
template <typename t> orerror<t> deserialise(buffer &);

/* Deserialisation context for version 1 of the protocol. */
class deserialise1 {
    /* Either the buffer we're deserialising or a quickcheck instance
     * if we're trying to generate random instances of the type. */
private: either<nnp<buffer>, nnp<quickcheck> > src;
    /* Either Success or the first first error we encountered. */
private: orerror<void> error;
public:  explicit deserialise1(buffer &_src);
public:  explicit deserialise1(quickcheck &_src);
    /* Deserialisers for fundamental types.  Other types have unary
     * constructors which do similar conversions.  Note that on error
     * these return 0 or false, as appropriate, rather than returning
     * an explicit error or raising an exception.  You should always
     * check the accumulated error before using the results for
     * anything important. */
public:  operator bool();
public:  operator char();
public:  operator unsigned char();
public:  operator short();
public:  operator unsigned short();
public:  operator int();
public:  operator unsigned int();
public:  operator long();
public:  operator unsigned long();
    /* Extract something by just copying the raw bits.  Only used to
     * implement the conversion operators. */
private: template <typename t> t pop();
    /* Mark the deserialiser as failed, if it hasn't already been
     * marked. */
public:  void fail(::error);
    /* Check for errors.  Should only usually be necessary at the
     * very end of the deserialisation. */
public:  orerror<void> status() const; };

/* Version-less serialiser. */
class serialisebase {
private:   buffer &dest;
protected: serialisebase(int version, buffer &_dest);
public:    explicit serialisebase(buffer &_dest);
    /* Push a fundamental type; mirror image of deserialise1::pop() */
public:    template <typename t> void push(t); };

/* Serialisation context for version 1. */
class serialise1 : public serialisebase {
protected: serialise1(short version, buffer &_dest);
public:    explicit serialise1(buffer &_dest);
public:    template <typename t> void push(t x) { serialisebase::push(x); } };

/* Hooks for testing upgrade before we have another version to upgrade
 * to. */
class deserialiseT : public deserialise1 {
public: explicit deserialiseT(buffer &_src); };
class serialiseT : public serialise1 {
public: explicit serialiseT(buffer &_src); };

namespace tests { void _serialise(); }

template <typename> void serialise(quickcheck &, unsigned nr = 1000);
template <typename t> t mkrandom(quickcheck &);

#endif /* !SERIALISE_H__ */
