/* Simple way of generating cryptographic message digests */
#ifndef DIGEST_H__
#define DIGEST_H__

#include "wireproto.H"

namespace fields {
class field;
}

class digest {
    unsigned long val;
    explicit digest(unsigned long _val);
public:
    WIREPROTO_TYPE(digest);
    digest(const fields::field &);
    bool operator==(const digest &o) const;
    bool operator!=(const digest &o) const;
};

#endif /* !DIGEST_H__ */
