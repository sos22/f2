#include "mastersecret.H"

#include <time.h>

#include "digest.H"
#include "fields.H"
#include "logging.H"
#include "nonce.H"
#include "peername.H"

mastersecret::mastersecret(const char *_secret)
    : secret(_secret)
{}

mastersecret::mastersecret(const mastersecret &o)
    : secret(o.secret)
{}

masternonce
mastersecret::nonce(const peername &slavename)
{
    return masternonce(digest("D" + fields::mk(time(NULL)) +
                              fields::mk(slavename) + secret));
}

bool
mastersecret::noncevalid(const masternonce &masternonce,
                         const peername &slavename) const
{
    time_t now(time(NULL));
    for (int i = 0; i < 10; i++) {
        /* Master nonces are valid for nine to ten seconds. */
        if (digest("D" + fields::mk(now-i) + fields::mk(slavename) + secret) ==
            masternonce.d) {
            if (i >= 7)
                logmsg(loglevel::info,
                       "using a nearly-expired master nonce (" + fields::mk(i) +
                       "/10 seconds)");
            return true;
        }
    }
    return false;
}

const fields::field &
fields::mk(const masternonce &ms)
{
    return fields::mk(ms.d);
}
