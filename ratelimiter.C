#include "ratelimiter.H"

#include "fields.H"
#include "timedelta.H"

ratelimiter::ratelimiter(const frequency &f, unsigned _bucket_size)
    : last_refill(timestamp::now()),
      max_rate(f),
      bucket_size(_bucket_size),
      bucket_content(_bucket_size),
      mux()
{}

void
ratelimiter::refill(mutex_t::token tok) const
{
    tok.formux(mux);
    auto now(timestamp::now());
    double nr_tokens = (now - last_refill) * max_rate;
    unsigned whole_tokens = nr_tokens;
    if (whole_tokens + bucket_content > bucket_size) {
        bucket_content = bucket_size;
        last_refill = now;
        return;
    }
    last_refill = last_refill + whole_tokens / max_rate;
    bucket_content += whole_tokens;
}

bool
ratelimiter::probe()
{
    mutex_t::token tok(mux.lock());
    if (bucket_content != 0) {
        bucket_content--;
        mux.unlock(&tok);
        return true;
    }
    refill(tok);
    if (bucket_content != 0) {
        bucket_content--;
        mux.unlock(&tok);
        return true;
    }
    mux.unlock(&tok);
    return false;
}

ratelimiter::operator ratelimiter_status() const
{
    auto token(mux.lock());
    refill(token);
    ratelimiter_status res(max_rate, bucket_size, bucket_content);
    mux.unlock(&token);
    return res;
}

const fields::field &
fields::mk(const ratelimiter_status &rs)
{
    return "<ratelimiterstatus max_rate=" + fields::mk(rs.max_rate)
        + " bucket_size=" + fields::mk(rs.bucket_size)
        + " bucket_content=" + fields::mk(rs.bucket_content)
        + ">";
}
