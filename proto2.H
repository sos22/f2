#ifndef PROTO2_H__
#define PROTO2_H__

#include "interfacetype.H"
#include "list.H"
#include "orerror.H"
#include "version.H"

namespace proto {
class sequencenr {
private: sequencenr() = delete;
private: unsigned long val;
public:  sequencenr(deserialise1 &);
public:  explicit sequencenr(unsigned long _val) : val(_val) {}
public:  void operator++(int) { val++; }
public:  void serialise(serialise1 &) const;
public:  bool operator==(sequencenr o) const { return val == o.val; }
public:  bool operator!=(sequencenr o) const { return val != o.val; } };

class reqheader {
private: reqheader() = delete;
    /* Includes this header.  rpcclient2::workerthread::queuetx()
     * assumes this is at the start of the serialised data. */
public:  unsigned size;
public:  version vers;
public:  interfacetype type;
public:  sequencenr seq;
public:  reqheader(unsigned _size,
                   version _vers,
                   interfacetype _type,
                   sequencenr _seq);
public:  explicit reqheader(deserialise1 &);
public:  void serialise(serialise1 &); };

/* response version always matches request version */
class respheader {
private: respheader() = delete;
    /* Includes this header.  rpcservice2::incompletecall::complete()
     * assumes this is at the start of the serialised data. */
public:  unsigned size;
public:  sequencenr seq;
public:  orerror<void> status;
public:  respheader(unsigned _size, sequencenr _seq, orerror<void> _status);
public:  explicit respheader(deserialise1 &);
public:  void serialise(serialise1 &); };

/* Arbitrarily limit message size, to catch silliness. */
const size_t maxmsgsize = 128ul << 20;

/* Special type for empty messages.  Occasionally useful for making
 * API more symmetrical. */
class empty {
public: empty() {}
public: empty(deserialise1 &) {}
public: void serialise(serialise1 &) {} };

namespace meta {
class tag {
private: unsigned v;
private: explicit tag(unsigned _v) : v(_v) {}
    /* Inputs: None.  Outputs: version min, version max,
     * list<interfacetype>, giving the mininum and maximum acceptable
     * protocol versions and all supported interfaces.  Only ever sent
     * at the very start of a connection. */
public:  static const tag hello;
    /* Abort a message.  This is quite special: the sequence number is
     * the same as the message being aborted and there is no reply at
     * all.  This is a best-effort abort only: the server is allowed
     * to abort the call, or it can process it to completion if that's
     * more convenient.  The server must still generate a reply to the
     * original message, even if it's aborted (in which case the
     * returned status should be error::aborted). */
public:  static const tag abort;
public:  bool operator==(tag o) const { return v == o.v; }
public:  bool operator!=(tag o) const { return v != o.v; }
public:  explicit tag(deserialise1 &ds);
public:  void serialise(serialise1 &s) const; }; } }

#endif /* !PROTO2_H__ */
