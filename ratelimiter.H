#ifndef RATELIMITER_H__
#define RATELIMITER_H__

#include "frequency.H"
#include "mutex.H"
#include "quickcheck.H"
#include "timestamp.H"
#include "wireproto.H"

class ratelimiterconfig {
public: frequency maxrate;
public: unsigned bucketsize;
public: ratelimiterconfig(frequency _maxrate,
                          unsigned _bucketsize)
    : maxrate(_maxrate),
      bucketsize(_bucketsize) {}
public: ratelimiterconfig(quickcheck q)
    : maxrate(q),
      bucketsize(q) {}
public: bool operator==(const ratelimiterconfig &o) const {
    return maxrate == o.maxrate &&
        bucketsize == o.bucketsize; }
    WIREPROTO_TYPE(ratelimiterconfig);
};

struct ratelimiter_status {
    WIREPROTO_TYPE(ratelimiter_status);

private: ratelimiter_status() = delete;
private: void operator=(const ratelimiter_status &) = delete;
public:
    const ratelimiterconfig config;
    const unsigned bucket_content;
    const unsigned dropped;
    ratelimiter_status(const ratelimiter_status &o)
        : config(o.config),
          bucket_content(o.bucket_content),
          dropped(o.dropped) {}
    ratelimiter_status(const ratelimiterconfig &_config,
                       const unsigned _bucket_content,
                       const unsigned _dropped)
        : config(_config),
          bucket_content(_bucket_content),
          dropped(_dropped)
        {}
    ratelimiter_status(const quickcheck &q)
        : config(q),
          bucket_content(q),
          dropped(q) {}
    bool operator==(const ratelimiter_status &o) const {
        return config == o.config &&
            bucket_content == o.bucket_content &&
            dropped == o.dropped; }
    ~ratelimiter_status() {}
};

class ratelimiter {
    ratelimiter() = delete;
    void operator = (const ratelimiter &) = delete;
    /* Lots of mutables because refilling the bucket changes our
       internal state without changing the visible state. */
    mutable timestamp last_refill;
    mutable unsigned bucket_content;
    mutable mutex_t mux;
    const ratelimiterconfig config;
    unsigned dropped;
    void refill(mutex_t::token) const;
public:
    ratelimiter(const ratelimiter &);
    ratelimiter(const ratelimiterconfig &);
    /* If there are tokens available, consume one and return true.
       Otherwise, return false. */
    bool probe() __attribute__((warn_unused_result));
    /* Wait for a token to become available and consume one. */
    void wait();
    ratelimiter_status status() const;
};

namespace fields {
class field;
const field &mk(const ratelimiterconfig &);
const field &mk(const ratelimiter_status &);
}

template <typename> class parser;
namespace parsers {
const parser<ratelimiterconfig> &_ratelimiterconfig();
}

namespace tests {
void ratelimiter();
}

#endif
