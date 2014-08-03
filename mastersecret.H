/* The beacon server and the controller need to share a secret for the
   authentication protocol to work, even though that secret is never
   shared with any other hosts.  This class implements that secret. */
#ifndef MASTERSECRET_H__
#define MASTERSECRET_H__

#include "digest.H"
#include "nonce.H"
#include "wireproto.H"

class masternonce;
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
    WIREPROTO_WRAPPER_TYPE(masternonce, digest, public);
public:
    bool operator==(const masternonce &o) const { return d == o.d; }
};

class mastersecret {
private: mastersecret() = delete;
private: class nonce secret;
public:  static mastersecret mk();
private: mastersecret(const class nonce &);
public:  mastersecret(const mastersecret &);
public:  void operator=(const mastersecret &);
public:  masternonce nonce(const peername &slavename);
public:  bool noncevalid(const masternonce &nonce,
                         const peername &slavename) const;
};

#endif /* MASTERSECRET_H__ */
