/* A thing for generating unpredictable values, suitable for use in
 * some communication protocols. */
#ifndef NONCE_H__
#define NONCE_H__

#include <stdint.h>

#include "wireproto.H"

class nonce;

namespace fields {
class field;
const fields::field &mk(const nonce &);
}

class nonce {
    friend const fields::field &fields::mk(const nonce &);
    uint64_t val;
    explicit nonce(uint64_t _val);
    uint64_t unwrap() const { return val; }
public:
    WIREPROTO_WRAPPER_TYPE(nonce, uint64_t);
    static nonce mk();
};

#endif /* !NONCE_H__ */
