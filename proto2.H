#ifndef PROTO2_H__
#define PROTO2_H__

#include "version.H"

namespace proto {
class sequencenr {
private: sequencenr() = delete;
private: unsigned long val;
public:  sequencenr(deserialise1 &);
public:  explicit sequencenr(unsigned long _val) : val(_val) {}
public:  void operator++(int) { val++; }
public:  void serialise(serialise1 &) const;
public:  bool operator==(sequencenr o) const { return val == o.val; } };

class reqheader {
private: reqheader() = delete;
    /* Includes this header.  rpcclient2::workerthread::queuetx()
     * assumes this is at the start of the serialised data. */
public:  unsigned size;
public:  version vers;
public:  sequencenr seq;
public:  reqheader(unsigned _size, version _vers, sequencenr _seq);
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

namespace hello {
typedef empty req;
class resp {
    /* Versions supported by the server.  Client either accepts these
     * or disconnects again. */
private: resp() = delete;
public:  version min;
public:  version max;
public:  resp(version _min, version _max);
public:  explicit resp(deserialise1 &);
public:  void serialise(serialise1 &); }; } }

#endif /* !PROTO2_H__ */
