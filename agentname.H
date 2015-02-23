/* Persistent names for the things which either store data or run
 * tasks.  These differ from peernames in that peernames identify
 * network endpoints, whereas these identify the things behind those
 * endpoints.  i.e. if you give something a new IP address it'll get a
 * new peername but keep its old agentname. */
#ifndef AGENTNAME_H__
#define AGENTNAME_H__

#include "string.H"

class quickcheck;
class agentname;
template <typename> class parser;

namespace fields {
class field;
const field &mk(const agentname &);
};

class agentname {
    friend const fields::field &fields::mk(const agentname &);
private: string content;
private: agentname() = delete;
public:  agentname(const agentname &);
public:  agentname(deserialise1 &);
public:  agentname(const string &s) : content(s) {}
public:  void serialise(serialise1 &) const;
public:  agentname steal();
public:  agentname(const quickcheck &);
public:  bool operator==(const agentname &) const;
public:  bool operator!=(const agentname &) const;
public:  const fields::field &field() const {
    return fields::mk(*this); }
};

namespace parsers {
extern const parser< ::agentname> &_agentname();
}

#endif /* !AGENTNAME_H__ */
