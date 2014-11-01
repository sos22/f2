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
class error;
class quickcheck;

/* Deserialisation context for version 1 of the protocol. */
class deserialise1 {
    /* Either the buffer we're deserialising or a quickcheck instance
     * if we're trying to generate random instances of the type. */
private: either<nnp<const buffer>, nnp<quickcheck> > src;
    /* Either Success or the first first error we encountered. */
private: orerror<void> error;
    /* Offset in buffer to start decoding the next field. */
private: unsigned long _offset;
public:  explicit deserialise1(const buffer &_src);
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
    /* Fill a buffer by simplying copying bytes out of the input. */
public:  void bytes(void *out, size_t sz);
    /* Extract something and validate that it falls into a particular
     * range.  This is usually better than doing the validation
     * yourself because it acts as a hint to the quickcheck
     * implementation that it should concentrate on valid values.  min
     * and max are both inclusive.  Type t needs to support the usual
     * arithmetic operations. */
public:  template <typename t> t poprange(t min, t max);

public: unsigned long offset() const { return _offset; }

    /* Mark the deserialiser as failed, if it hasn't already been
     * marked. */
public:  void fail(::error);
    /* Check for errors.  Should only usually be necessary at the
     * very end of the deserialisation. */
public:  orerror<void> status() const;
    /* Convenience wrappers for status() */
public:  bool issuccess() const { return status().issuccess(); }
public:  bool isfailure() const { return status().isfailure(); }
public:  ::error failure() const { return status().failure(); } };

/* Version-less serialiser. */
class serialise1 {
private:   buffer &dest;
public:    explicit serialise1(buffer &_dest);
    /* Push a fundamental type; mirror image of deserialise1::pop() */
public:    void push(bool);
public:    void push(char);
public:    void push(unsigned char);
public:    void push(short);
public:    void push(unsigned short);
public:    void push(int);
public:    void push(unsigned int);
public:    void push(long);
public:    void push(unsigned long);
    /* Only used to implement the push methods. */
private:   template <typename t> void pushfundamental(t);
    /* Copy some raw bytes into the buffer. */
public:    void bytes(const void *, size_t); };

/* Hooks for testing upgrade before we have another version to upgrade
 * to. */
class deserialiseT : public deserialise1 {
public: explicit deserialiseT(buffer &_src); };
class serialiseT : public serialise1 {
public: explicit serialiseT(buffer &_src); };

namespace tests { void _serialise(); }

template <typename t> t mkrandom(quickcheck &);

#endif /* !SERIALISE_H__ */
