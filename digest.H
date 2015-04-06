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

class digest {
    friend const fields::field &fields::mk(const digest &d);
    unsigned long val;
private: explicit digest(unsigned long _val) : val(_val) {}
public:  explicit digest(const fields::field &);
public:  explicit digest(deserialise1 &);
public:  void serialise(serialise1 &) const;

public:  bool operator<(const digest &o) const { return val < o.val; }
public:  bool operator<=(const digest &o) const { return val <= o.val; }
public:  bool operator==(const digest &o) const { return val == o.val; }
public:  bool operator!=(const digest &o) const { return val != o.val; }
public:  bool operator>=(const digest &o) const { return val >= o.val; }
public:  bool operator>(const digest &o) const { return val > o.val; }

public:  static const ::parser<digest> &parser();

    /* A representation of the digest which is reasonably dense and
       composed entirely of ASCII characters. */
public:  string denseprintable() const;
};

#endif /* !DIGEST_H__ */
