/* Simple way of generating cryptographic message digests */
#ifndef DIGEST_H__
#define DIGEST_H__

#include "wireproto.H"

class digest;
template <typename> class parser;
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
    WIREPROTO_WRAPPER_TYPE(digest, unsigned long, private);
public:
    digest(const fields::field &);
    bool operator==(const digest &o) const;
    bool operator!=(const digest &o) const;

    /* A representation of the digest which is reasonably dense and
       composed entirely of ASCII characters. */
    string denseprintable() const;
};

#endif /* !DIGEST_H__ */
