#include "nonce.H"

#include <stdlib.h>

#include "fields.H"
#include "logging.H"

nonce::nonce(uint64_t _val)
    : val(_val)
{}

nonce
nonce::mk()
{
    /* This is insecure, but it's easy to implement, and will do for
     * now. */
    logmsg(loglevel::error,
           fields::mk("nonces should use a stronger random number generator"));
    return nonce((uint64_t)random() ^
                 ((uint64_t)random() << 22) ^
                 ((uint64_t)random() << 44));
}

const fields::field &
fields::mk(const nonce &n)
{
    return "<nonce:" +
        fields::padleft(
            fields::mk(n.val).base(16).uppercase(),
            16,
            fields::mk("0")) +
        ">";
}
