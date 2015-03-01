#ifndef PROTO_H__
#define PROTO_H__

class deserialise1;
class serialise1;

namespace proto {
/* Tags used to distinguish calls against a given protocol.  Only
 * useful once it's been derived from. */
class tag {
private:   unsigned char d;
protected: explicit tag(unsigned char _d) : d(_d) {}
public:    explicit tag(deserialise1 &);
public:    void serialise(serialise1 &) const;
public:    bool operator==(tag o) const { return d == o.d; }
public:    bool operator!=(tag o) const { return d != o.d; } }; }

#endif /* !PROTO_H__ */
