/* The beacon server and the controller need to share a secret for the
   authentication protocol to work, even though that secret is never
   shared with any other hosts.  This class implements that secret. */
#ifndef MASTERSECRET_H__
#define MASTERSECRET_H__

#include "digest.H"
#include "wireproto.H"

class masternonce;
class nonce;
class peername;
class registrationsecret;

namespace fields {
class field;
const field &mk(const masternonce &);
}

class masternonce {
    friend class mastersecret;
    friend const fields::field &fields::mk(const masternonce &);
    digest d;
public:
    WIREPROTO_TYPE(masternonce);

    explicit masternonce(const digest &d);
};

class mastersecret {
    const char *secret;
    mastersecret() = delete;
    void operator=(const mastersecret &) = delete;
public:
    explicit mastersecret(const mastersecret &);
    explicit mastersecret(const char *);
    masternonce nonce(const peername &slavename);
    bool noncevalid(const masternonce &nonce,
                    const peername &slavename) const;
};

#endif /* MASTERSECRET_H__ */
