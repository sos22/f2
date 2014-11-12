/* Simple way of generating cryptographic message digests */
#ifndef DIGEST_H__
#define DIGEST_H__

class deserialise1;
class digest;
template <typename> class parser;
class serialise1;
class string;

namespace fields {
class field;
const field &mk(const digest &d);
}

namespace parsers {
const parser< ::digest> &_digest();
}

class digest {
    friend const fields::field &fields::mk(const digest &d);
    friend const parser< ::digest> &parsers::_digest();
    unsigned long val;
private: explicit digest(unsigned long _val) : val(_val) {}
public:  explicit digest(const fields::field &);
public:  explicit digest(deserialise1 &);
public:  void serialise(serialise1 &) const;

    bool operator==(const digest &o) const;
    bool operator!=(const digest &o) const;

    /* A representation of the digest which is reasonably dense and
       composed entirely of ASCII characters. */
    string denseprintable() const;

public:  bool operator<(const digest &o) const;
public:  bool operator>(const digest &o) const;
};

#endif /* !DIGEST_H__ */
