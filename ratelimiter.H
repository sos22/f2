#ifndef RATELIMITER_H__
#define RATELIMITER_H__

#include "frequency.H"
#include "mutex.H"
#include "quickcheck.H"
#include "timestamp.H"
#include "wireproto.H"

struct ratelimiter_status {
    WIREPROTO_TYPE(ratelimiter_status);

    const frequency max_rate;
    const unsigned bucket_size;
    const unsigned bucket_content;
    const unsigned dropped;
    ratelimiter_status(const frequency &_max_rate,
                       const unsigned _bucket_size,
                       const unsigned _bucket_content,
                       const unsigned _dropped)
        : max_rate(_max_rate), bucket_size(_bucket_size),
          bucket_content(_bucket_content), dropped(_dropped)
        {}
    ratelimiter_status(const quickcheck &q)
        : max_rate(q),
          bucket_size(q),
          bucket_content(q),
          dropped(q) {}
    bool operator==(const ratelimiter_status &o) const {
        return max_rate == o.max_rate &&
            bucket_size == o.bucket_size &&
            bucket_content == o.bucket_content &&
            dropped == o.dropped; }
};

class ratelimiter {
    ratelimiter() = delete;
    ratelimiter(const ratelimiter &) = delete;
    void operator = (const ratelimiter &) = delete;
    /* Lots of mutables because refilling the bucket changes our
       internal state without changing the visible state. */
    mutable timestamp last_refill;
    frequency const max_rate;
    unsigned const bucket_size;
    mutable unsigned bucket_content;
    mutable mutex_t mux;
    unsigned dropped;
    void refill(mutex_t::token) const;
public:
    ratelimiter(const frequency &, unsigned bucket_size);
    /* If there are tokens available, consume one and return true.
       Otherwise, return false. */
    bool probe() __attribute__((warn_unused_result));

    ratelimiter_status status() const;
};

namespace fields {
class field;
const field &mk(const ratelimiter_status &);
}

#endif
