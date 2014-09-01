#include "ratelimiter.H"

#include "fields.H"
#include "logging.H"
#include "parsers.H"
#include "test.H"
#include "timedelta.H"

#include "parsers.tmpl"
#include "wireproto.tmpl"

ratelimiter::ratelimiter(const ratelimiterconfig &_config)
    : last_refill(timestamp::now()),
      bucket_content(_config.bucketsize),
      mux(),
      config(_config),
      dropped(0)
{}

ratelimiter::ratelimiter(const ratelimiter &o)
    : last_refill(o.last_refill), /* Will clobber this with correctly
                                   * synchronised load later. */
      mux(),
      config(o.config) {
    /* Not entirely happy about taking this lock from a constructor,
       but it's a short leaf lock, so we should be okay. */
    auto token(o.mux.lock());
    /* Might as well do it now to save having to refill both limiters
     * later. */
    o.refill(token);
    last_refill = o.last_refill;
    bucket_content = o.bucket_content;
    dropped = o.dropped;
    o.mux.unlock(&token); }

void
ratelimiter::refill(mutex_t::token tok) const
{
    tok.formux(mux);
    auto now(timestamp::now());
    double nr_tokens = (now - last_refill) * config.maxrate;
    unsigned whole_tokens = (unsigned)nr_tokens;
    if (whole_tokens + bucket_content > config.bucketsize) {
        bucket_content = config.bucketsize;
        last_refill = now;
        return;
    }
    last_refill = last_refill + whole_tokens / config.maxrate;
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

void
ratelimiter::wait() {
    mutex_t::token tok(mux.lock());
    if (bucket_content != 0) {
        bucket_content--;
        mux.unlock(&tok);
        return; }
    while (true) {
        refill(tok);
        if (bucket_content != 0) {
            bucket_content--;
            mux.unlock(&tok);
            return; }
        /* Drop lock for the actual sleep so that probe() doesn't have
           to wait for the bucket to refill. */
        auto sleepto(last_refill + (1.0 / config.maxrate));
        mux.unlock(&tok);
        sleepto.sleep();
        tok = mux.lock(); } }

ratelimiter_status
ratelimiter::status() const
{
    auto token(mux.lock());
    refill(token);
    ratelimiter_status res(config, bucket_content, dropped);
    mux.unlock(&token);
    return res;
}

const fields::field &
fields::mk(const ratelimiterconfig &rc) {
    return "<ratelimiterconfig: maxrate=" + fields::mk(rc.maxrate) +
        " bucketsize=" + fields::mk(rc.bucketsize) +
        ">"; }
const parser<ratelimiterconfig> &
parsers::_ratelimiterconfig() {
    return ("<ratelimiterconfig: maxrate=" + _frequency() +
            " bucketsize=" + intparser<unsigned>() +
            ">")
        .map<ratelimiterconfig>([] (pair<frequency, unsigned> x) {
                return ratelimiterconfig(x.first(), x.second()); }); }

const fields::field &
fields::mk(const ratelimiter_status &rs)
{
    return "<ratelimiterstatus config=" + fields::mk(rs.config)
        + " bucket_content=" + fields::mk(rs.bucket_content)
        + " dropped=" + fields::mk(rs.dropped)
        + ">";
}

namespace proto {
    namespace ratelimiterconfig {
        static const wireproto::parameter<frequency> maxrate(1);
        static const wireproto::parameter<unsigned> bucketsize(2);
    }
    namespace ratelimiter_status {
        static const wireproto::parameter< ::ratelimiterconfig> config(1);
        static const wireproto::parameter<unsigned> bucket_content(2);
        static const wireproto::parameter<unsigned> dropped(3);
    }
}

wireproto_wrapper_type(ratelimiterconfig)
void
ratelimiterconfig::addparam(wireproto::parameter<ratelimiterconfig> tmpl,
                            wireproto::tx_message &tx_msg) const
{
    tx_msg.addparam(wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
                    wireproto::tx_compoundparameter()
                    .addparam(proto::ratelimiterconfig::maxrate, maxrate)
                    .addparam(proto::ratelimiterconfig::bucketsize,
                              bucketsize)); }
maybe<ratelimiterconfig>
ratelimiterconfig::fromcompound(const wireproto::rx_message &msg)
{
    auto maxrate(msg.getparam(proto::ratelimiterconfig::maxrate));
    auto bucketsize(msg.getparam(proto::ratelimiterconfig::bucketsize));
    if (!maxrate || !bucketsize) return Nothing;
    return ratelimiterconfig(maxrate.just(), bucketsize.just()); }

wireproto_wrapper_type(ratelimiter_status)
void
ratelimiter_status::addparam(wireproto::parameter<ratelimiter_status> tmpl,
                             wireproto::tx_message &tx_msg) const
{
    tx_msg.addparam(wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
                    wireproto::tx_compoundparameter()
                    .addparam(proto::ratelimiter_status::config, config)
                    .addparam(proto::ratelimiter_status::bucket_content,
                              bucket_content)
                    .addparam(proto::ratelimiter_status::dropped,
                              dropped));
}
maybe<ratelimiter_status>
ratelimiter_status::fromcompound(const wireproto::rx_message &msg)
{
    auto config(msg.getparam(proto::ratelimiter_status::config));
    auto bucket_content(
        msg.getparam(proto::ratelimiter_status::bucket_content));
    auto dropped(msg.getparam(proto::ratelimiter_status::dropped));
    if (!config || !bucket_content || !dropped) return Nothing;
    return ratelimiter_status(config.just(),
                              bucket_content.just(),
                              dropped.just());
}

void
tests::ratelimiter() {
    testcaseV("ratelimiter", "smallbucket", [] () {
            /* Set bucket size to one and hit it as hard as
             * possible. */
            auto freq(frequency::hz(2000));
            ::ratelimiter r(ratelimiterconfig(freq, 1));
            int count(0);
            timestamp start(timestamp::now());
            timestamp lastsuccess(timestamp::now());
            while (count < 4000) {
                auto res(r.probe());
                count += res ? 1 : 0;
                auto n(timestamp::now());
                /* Should get a token every 500us.  Pretend it's 20ms
                   to allow for timer oddities and getting preempted
                   in a bad place. */
                assert(n - lastsuccess < timedelta::milliseconds(20));
                /* Should get roughly the right number of tokens. */
                /* Small buckets won't track the target frequency
                   particularly accurate (in particular, we'll go
                   below target if we ever lose tokens to bucket
                   overflow).  There should be a fairly tight bound on
                   how far over we can go though (one for the initial
                   bucket contents, plus a small number for the @n
                   timestamp not quite matching the one used for
                   bucket refill).  Reflect that in an asymmetric
                   acceptable range. */
                assert(count >= (int)((n - start) * freq) - 100);
                assert(count <= (int)((n - start) * freq) + 5);
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
                    ratelimiterconfig(
                        freq,
                        (unsigned)((burstduration * (burstrate - baserate)) *
                                   ratio) + 1));
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
            ::ratelimiter r(ratelimiterconfig(frequency::hz(10), 5));
            assert(r.status() == r.status());
            ratelimiter_status copy(r.status());
            assert(copy == r.status());
            ratelimiter_status copy2(copy);
            assert(copy2 == r.status());
            assert(!(r.status() == ratelimiter_status(quickcheck()))); });

    testcaseV("ratelimiter", "wireproto", [] {
            wireproto::roundtrip<ratelimiter_status>();
            wireproto::roundtrip<ratelimiterconfig>(); });

    testcaseV("ratelimiter", "parsers", [] {
            parsers::roundtrip(parsers::_ratelimiterconfig()); });

    testcaseV("ratelimiter", "dupewait", [] {
            ::ratelimiter r(ratelimiterconfig(frequency::hz(1000), 100));
            int cntr = 0;
            auto start(timestamp::now());
            while (timestamp::now() < start + timedelta::milliseconds(100)) {
                r.wait();
                cntr++; }
            assert(cntr >= 180);
            assert(cntr <= 220);
            ::ratelimiter r2(r);
            while (timestamp::now() < start + timedelta::milliseconds(200)) {
                r2.wait();
                cntr++; }
            assert(cntr >= 280);
            assert(cntr <= 320);
            ::ratelimiter r3(r);
            while (timestamp::now() < start + timedelta::milliseconds(300)) {
                r3.wait();
                cntr++; }
            assert(cntr >= 480);
            assert(cntr <= 520); });
}
