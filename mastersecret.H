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
    digest unwrap() const { return d; }
public:
    WIREPROTO_WRAPPER_TYPE(masternonce, digest);

    explicit masternonce(const digest &d);
    bool operator==(const masternonce &o) const { return d == o.d; }
};

class mastersecret {
    const char *secret;
    mastersecret() = delete;
public:
    explicit mastersecret(const mastersecret &);
    void operator=(const mastersecret &);
    /* XXX there's no reason to do it this way; we'd be better off
       generating master secrets at random at startup time. */
    explicit mastersecret(const char *);
    masternonce nonce(const peername &slavename);
    bool noncevalid(const masternonce &nonce,
                    const peername &slavename) const;
};

#endif /* MASTERSECRET_H__ */
