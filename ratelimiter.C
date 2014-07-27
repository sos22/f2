#include "ratelimiter.H"

#include "fields.H"
#include "logging.H"
#include "test.H"
#include "timedelta.H"

ratelimiter::ratelimiter(const frequency &f, unsigned _bucket_size)
    : last_refill(timestamp::now()),
      max_rate(f),
      bucket_size(_bucket_size),
      bucket_content(_bucket_size),
      mux(),
      dropped(0)
{}

void
ratelimiter::refill(mutex_t::token tok) const
{
    tok.formux(mux);
    auto now(timestamp::now());
    double nr_tokens = (now - last_refill) * max_rate;
    unsigned whole_tokens = (unsigned)nr_tokens;
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
    dropped++;
    mux.unlock(&tok);
    return false;
}

ratelimiter_status
ratelimiter::status() const
{
    auto token(mux.lock());
    refill(token);
    ratelimiter_status res(max_rate, bucket_size, bucket_content, dropped);
    mux.unlock(&token);
    return res;
}

const fields::field &
fields::mk(const ratelimiter_status &rs)
{
    return "<ratelimiterstatus max_rate=" + fields::mk(rs.max_rate)
        + " bucket_size=" + fields::mk(rs.bucket_size)
        + " bucket_content=" + fields::mk(rs.bucket_content)
        + " dropped=" + fields::mk(rs.dropped)
        + ">";
}

void
tests::ratelimiter() {
    testcaseV("ratelimiter", "smallbucket", [] () {
            /* Set bucket size to one and hit it as hard as
             * possible. */
            auto freq(frequency::hz(2000));
            ::ratelimiter r(freq, 1);
            int count(0);
            timestamp start(timestamp::now());
            timestamp lastsuccess(timestamp::now());
            while (count < 4000) {
                auto res(r.probe());
                count += res ? 1 : 0;
                auto n(timestamp::now());
                /* Should get a token every 10ms.  Pretend it's 20ms
                   to allow for timer oddities. */
                assert(n - lastsuccess < timedelta::milliseconds(20));
                /* Should get roughly the right number of tokens. */
                assert(count >= (int)((n - start) * freq) - 50);
                assert(count <= (int)((n - start) * freq) + 50);
                if (res) lastsuccess = n; } });
    auto bursty(
        [] (bool over) {
            initlogging("test");
            double ratio = over ? 0.8 : 1.2;
            for (unsigned x = 0; x < 10; x++) {
                auto baserate(
                    frequency::hz(((int)(random() % 10) + 1) * 10.0));
                auto burstrate(
                    baserate +
                    frequency::hz(((int)(random() % 100) + 100) * 1000.0));
                auto burstduration(timedelta::microseconds(random() % 100+100));
                auto burstinterval(timedelta::microseconds(random() % 10000));
                ::logmsg(loglevel::notice,
                         fields::mk(x)+"/"+fields::mk(10)+
                         ": baserate="+fields::mk(baserate)+
                         " burstrate:"+fields::mk(burstrate)+
                         " burstduration:"+fields::mk(burstduration)+
                         " burstinterval:"+fields::mk(burstinterval));
                /* Size limiter so that it either allows or disallows
                 * the workload. */
                auto freq(
                    baserate + (burstrate * (burstduration /
                                             (burstduration + burstinterval))) *
                    ratio);
                ::ratelimiter r(
                    freq,
                    (unsigned)((burstduration * (burstrate - baserate)) *
                               ratio) + 1);
                auto starttime(timestamp::now());
                unsigned allowed = 0;
                unsigned blocked = 0;
                while (timestamp::now() - starttime <
                       timedelta::milliseconds(500)) {
                    /* Start with a burst. */
                    {   auto burststart(timestamp::now());
                        auto burstend(burststart + burstduration);
                        unsigned burstnr = 0;
                        while (timestamp::now() < burstend) {
                            double targetburstnr =
                                (timestamp::now() - burststart) * burstrate;
                            if (burstnr > targetburstnr) continue;
                            (burststart + (targetburstnr - burstnr) / burstrate)
                                .sleep();
                            if (r.probe()) allowed++;
                            else blocked++;
                            burstnr++; } }
                    /* Now go slow for a bit. */
                    {   auto slowstart(timestamp::now());
                        auto slowend(slowstart + burstinterval);
                        unsigned slownr = 0;
                        while (timestamp::now() < slowend) {
                            double targetslownr =
                                (timestamp::now() - slowstart) * baserate;
                            if (slownr > targetslownr) continue;
                            (slowstart + (targetslownr - slownr) / baserate)
                                .sleep();
                            if (r.probe()) allowed++;
                            else blocked++;
                            slownr++; } } }
                auto finish(timestamp::now());
                ::logmsg(loglevel::info,
                         "--> allowed " + fields::mk(allowed) +
                         ", blocked " + fields::mk(blocked) +
                         ", over " + fields::mk(over) +
                         ", limiter " + fields::mk(r.status()));
                assert(allowed < (finish - starttime) * freq * 1.1);
                if (over) assert(allowed > (finish - starttime) * freq * 0.9);
                else assert(blocked < (allowed + blocked) * 0.05); }
            deinitlogging(); });

    testcaseV("ratelimiter", "burstygood",
              [&bursty] () { bursty(false); });

    testcaseV("ratelimiter", "burstybad",
              [&bursty] () { bursty(true); });

    testcaseV("ratelimiter", "eq", [] () {
            ::ratelimiter r(frequency::hz(10), 5);
            assert(r.status() == r.status());
            ratelimiter_status copy(r.status());
            assert(copy == r.status());
            ratelimiter_status copy2(copy);
            assert(copy2 == r.status());
            assert(!(r.status() == ratelimiter_status(quickcheck()))); }); }
