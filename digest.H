/* Simple way of generating cryptographic message digests */
#ifndef DIGEST_H__
#define DIGEST_H__

#include "wireproto.H"

class digest;

namespace fields {
class field;
const field &mk(const digest &d);
}

class digest {
    friend const fields::field &fields::mk(const digest &d);
    unsigned long val;
    explicit digest(unsigned long _val);
    unsigned long unwrap() const { return val; }
public:
    WIREPROTO_WRAPPER_TYPE(digest, unsigned long);
    digest(const fields::field &);
    bool operator==(const digest &o) const;
    bool operator!=(const digest &o) const;
};

#endif /* !DIGEST_H__ */
