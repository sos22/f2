#include "pubsub.H"
#include "socket.H"
#include "spark.H"
#include "test.H"
#include "test2.H"
#include "timedelta.H"

#include "orerror.tmpl"
#include "pair.tmpl"
#include "spark.tmpl"
#include "test.tmpl"
#include "test2.tmpl"
#include "timedelta.tmpl"

static const auto epsilon(50_ms);
static const auto _io(clientio::CLIENTIO);

static testmodule __testpubsub(
    "pubsub",
    list<filename>::mk("pubsub.C", "pubsub.H"),
    testmodule::LineCoverage(97_pc),
    testmodule::BranchCoverage(70_pc),
    /* None of these take clientio tokens, because if clientio tests
     * start the pubsub thread from the harness, and that's something
     * we need to be able to control from the tests. */
    "pubcons", [] { publisher p; },
    "pubpub", [] { publisher().publish(); },
    "subcons", [] { subscriber s; },
    "subemptytimeout", [] {
        assert(subscriber().wait(_io, timestamp::now()) == NULL);
        assert(subscriber().wait(_io,epsilon.future())==NULL); },
    "basic1", [] {
        publisher p;
        subscriber s;
        subscription a(s, p);
        assert(s.wait(_io, epsilon.future()) == NULL); },
    "nonconcurrentnotification", [] {
        publisher p;
        subscriber s;
        subscription a(s, p);
        p.publish();
        assert(s.wait(_io, timestamp::now()) == &a);
        assert(s.wait(_io, epsilon.future()) == NULL); },
    "multinonconcurrentnotification", [] {
        publisher p;
        subscriber s;
        subscription a(s, p);
        p.publish();
        p.publish();
        assert(s.wait(_io, timestamp::now()) == &a);
        assert(s.wait(_io, epsilon.future()) == NULL);
        p.publish();
        assert(s.wait(_io, timestamp::now()) == &a);
        assert(s.wait(_io, epsilon.future()) == NULL); },
    "notificationafterunsub", [] {
        publisher p;
        subscriber s;
        {   subscription a(s, p);
            p.publish();
            assert(s.wait(_io, timestamp::now()) == &a); }
        assert(s.wait(_io, epsilon.future()) == NULL);
        p.publish();
        assert(s.wait(_io, epsilon.future()) == NULL); },
    "1pub1subNsubS", [] {
        publisher p;
        subscriber s;
        {   subscription a(s, p);
            {   subscription b(s, p);
                p.publish();
                auto f(s.wait(_io, timestamp::now()));
                auto g(s.wait(_io, timestamp::now()));
                auto h(s.wait(_io, epsilon.future()));
                assert(f != g);
                assert(f == &a || f == &b);
                assert(g == &a || g == &b);
                assert(h == NULL); }
            assert(s.wait(_io, epsilon.future()) == NULL);
            p.publish();
            assert(s.wait(_io, timestamp::now()) == &a);
            assert(s.wait(_io, epsilon.future()) == NULL); }
        p.publish();
        assert(s.wait(_io, epsilon.future()) == NULL); },
    "unsubnotified", [] {
        publisher p;
        subscriber s;
        {   subscription a(s, p);
            {   subscription b(s, p);
                p.publish(); }
            assert(s.wait(_io, timestamp::now()) == &a);
            assert(s.wait(_io, epsilon.future()) == NULL);
            p.publish();
            assert(s.wait(_io, timestamp::now()) == &a);
            assert(s.wait(_io, epsilon.future()) == NULL); } },
    "1pub2sub", [] {
        publisher p;
        subscriber s1;
        subscription a(s1, p);
        {   subscriber s2;
            subscription b(s2, p);
            p.publish();
            assert(s1.wait(_io, timestamp::now()) == &a);
            assert(s1.wait(_io, epsilon.future()) == NULL);
            assert(s2.wait(_io, timestamp::now()) == &b);
            assert(s2.wait(_io, epsilon.future()) == NULL); }
        p.publish();
        assert(s1.wait(_io, timestamp::now()) == &a);
        assert(s1.wait(_io, epsilon.future()) == NULL); },
    "2pub1sub", [] {
        publisher p1;
        publisher p2;
        subscriber s;
        subscription a(s, p1);
        subscription b(s, p2);
        assert(s.wait(_io, epsilon.future()) == NULL);
        p1.publish();
        assert(s.wait(_io, timestamp::now()) == &a);
        assert(s.wait(_io, epsilon.future()) == NULL);
        p2.publish();
        assert(s.wait(_io, timestamp::now()) == &b);
        assert(s.wait(_io, epsilon.future()) == NULL);
        p1.publish();
        p2.publish();
        auto f(s.wait(_io, timestamp::now()));
        auto g(s.wait(_io, timestamp::now()));
        auto h(s.wait(_io, epsilon.future()));
        assert(f != g);
        assert(f == &a || f == &b);
        assert(g == &a || g == &b);
        assert(h == NULL); },
    "basicIO", [] {
        initpubsub();
        {   subscriber s;
            auto p(fd_t::pipe());
            if (p.isfailure()) p.failure().fatal("creating pipe");
            iosubscription sub(s, p.success().read.poll(POLLIN));
            assert(s.wait(_io, epsilon.future()) == NULL);
            p.success().write.write(_io, "foo", 3);
            /* This should arguably be asserting that sub becomes
             * ready immediately, rather than that it becomes ready
             * after epsilon, because it is, but that's much harder to
             * implement and won't matter for any real users of the
             * interface. */
            assert(s.wait(_io, epsilon.future()) == &sub);

            assert(s.wait(_io, epsilon.future()) == NULL);
            sub.rearm();
            assert(s.wait(_io, timestamp::now()) == &sub);
            assert(s.wait(_io, epsilon.future()) == NULL);
            char buf[3];
            sub.rearm();
            p.success().read.read(_io, buf, 1);
            assert(s.wait(_io, timestamp::now()) == &sub);
            assert(s.wait(_io, epsilon.future()) == NULL);
            sub.rearm();
            assert(s.wait(_io, timestamp::now()) == &sub);
            assert(s.wait(_io, epsilon.future()) == NULL);
            sub.rearm();
            p.success().read.read(_io, buf, 2);
            assert(s.wait(_io, timestamp::now()) == &sub);
            assert(s.wait(_io, epsilon.future()) == NULL);
            sub.rearm();
            assert(s.wait(_io, epsilon.future()) == NULL); }
        deinitpubsub(_io); },
    "race pub+unsub", [] {
        /* One thread constantly publishes the condition in a tight
           loop, the other constantly subscribes and unsubscribes.
           Doesn't really do a great deal beyond confirming that we
           don't crash. */
        publisher pub;
        timestamp deadline(timedelta::milliseconds(200).future());
        int cntr;
        cntr = 0;
        spark<bool> t1([&pub, deadline] () {
                while (timestamp::now() < deadline) {
                    pub.publish(); }
                return true; });
        spark<bool> t2([&pub, &cntr, deadline] () {
                subscriber sub;
                while (timestamp::now() < deadline) {
                    subscription ss(sub, pub);
                    timestamp startwait(timestamp::now());
                    sub.wait(_io, deadline);
                    timestamp endwait(timestamp::now());
                    assert(endwait - startwait <
                           timedelta::milliseconds(50));
                    cntr++; }
                return true; } );
        t1.get();
        t2.get();
        assert(cntr >= 5000); },
    "pingpong", [] {
        /* Ping-pong back and forth between two threads for a
         * bit. */
        bool thread1;
        publisher pub1;
        publisher pub2;
        timestamp deadline(timestamp::now() + timedelta::milliseconds(200));
        thread1 = true;
        int cntr1;
        int cntr2;
        cntr1 = 0;
        cntr2 = 0;
        spark<void> t1([&thread1, &pub1, &pub2, deadline, &cntr1] {
                subscriber sub;
                subscription ss(sub, pub1);
                while (timestamp::now() < deadline) {
                    assert(timedelta::time<bool>(
                               [&thread1, &ss, &sub, deadline] () {
                                   while (!thread1) {
                                       auto r(sub.wait(_io, deadline));
                                       assert(
                                           r == &ss ||
                                           timestamp::now() >= deadline); }
                                   return true; }).td
                           < timedelta::milliseconds(10));
                    assert(thread1);
                    thread1 = false;
                    pub2.publish();
                    cntr1++; } });
        spark<void> t2([&thread1, &pub1, &pub2, &cntr2, deadline] {
                subscriber sub;
                subscription ss(sub, pub2);
                while (timestamp::now() < deadline) {
                    assert(timedelta::time<bool>(
                               [&thread1, &ss, &sub, deadline] () {
                                   while (thread1) {
                                       auto r(sub.wait(_io, deadline));
                                       assert(
                                           r == &ss ||
                                           timestamp::now() >= deadline); }
                                   return true; }).td
                           < timedelta::milliseconds(10));
                    assert(!thread1);
                    thread1 = true;
                    pub1.publish();
                    cntr2++; } });
        t1.get();
        t2.get();
        assert(cntr1 - cntr2 >= -1 && cntr1 - cntr2 <= 1);
        assert(cntr1 >= 5000); },
    "ioshutdownrace", [] {
        auto pipe(fd_t::pipe());
        subscriber sub;
        initpubsub();
        {   iosubscription ios(sub, pipe.success().read.poll(POLLIN));
            (10_ms).future().sleep(_io); }
        pipe.success().close();
        (10_ms).future().sleep(_io);
        deinitpubsub(_io); },
#if TESTING
    "iounsubscriberace", [] {
        auto pipe(fd_t::pipe());
        timestamp deadline(timestamp::now() + timedelta::seconds(1));
        initpubsub();
        /* We get a slightly different path through the
           iosubscription logic if an FD becomes readable at the
           iosubdetachrace event.  Give it a quick once-over. */
        tests::eventwaiter<void> evt1(
            tests::iosubdetachrace,
            [fd = pipe.success().write] () {
                fd.write(_io, "X", 1);
                (10_ms).future().sleep(_io);});
        spark<void> reader([fd = pipe.success().read, deadline] () {
                subscriber sub;
                iosubscription ios(sub, fd.poll(POLLIN)); });
        reader.get();
        deinitpubsub(_io); },
#endif
    "iosublots", [] {
        fd_t::piperes pipes[32];
        initpubsub();
        subscriber ss;
        list<iosubscription *> subs;
        for (int i = 0; i < 32; i++) {
            pipes[i] = fd_t::pipe().success();
            subs.pushtail(new iosubscription(ss,
                                             pipes[i].read.poll(POLLIN))); }
        for (int i = 0; i < 32; i++) {
            pipes[i].write.write(_io, "X", 1);
            auto r(ss.wait(_io));
            assert(r == subs.pophead());
            delete (iosubscription *)r; }
        deinitpubsub(_io); },

    "lateunsub", [] {
        publisher pub;
        subscription *ss;
        {   subscriber sub;
            ss = new subscription(sub, pub); }
        delete ss; },

    "detachnotifyrace", [] {
        initpubsub();
        auto pipe(fd_t::pipe().fatal("pipe()"));
        volatile bool shutdown(false);
        spark<void> bounce([&pipe, &shutdown] {
                while (!shutdown) {
                    (void)pipe.write.write(_io, "X", 1);
                    char buf;
                    (void)pipe.read.read(_io, &buf, 1); } });
        spark<void> watcher([&pipe, &shutdown] {
                while (!shutdown) {
                    subscriber sub;
                    char buf[sizeof(iosubscription)];
                    auto r = new (buf) iosubscription(
                        sub, pipe.read.poll(POLLIN));
                    /* Give it a chance to actually register. */
                    if (!sub.poll()) pthread_yield();
                    r->~iosubscription();
                    memset(buf, 0x99, sizeof(buf)); } });
        (5_s).future().sleep(_io);
        shutdown = true;
        watcher.get();
        deinitpubsub(_io); },
    "listenclosed", [] {
        initpubsub();
        auto pipe(fd_t::pipe().fatal("pipe()"));
        spark<pair<timestamp, timestamp> > listen([&pipe] {
                subscriber sub;
                iosubscription ios(sub, pipe.read.poll(POLLIN));
                (100_ms).future().sleep(_io);
                auto subtime(timestamp::now());
                sub.wait(_io);
                return mkpair(subtime, timestamp::now()); });
        (200_ms).future().sleep(_io);
        auto closeread(timestamp::now());
        pipe.read.close();
        (200_ms).future().sleep(_io);
        auto closewrite(timestamp::now());
        pipe.write.close();
        auto t(listen.get());
        assert(t.first() < closeread);
        assert(closewrite < t.second());
        deinitpubsub(_io); },
    "listenclosed2", [] {
        /* listen on fd0 and fd1 at the same time, close fd1,
           confirm that making fd0 readable unblocks the stall. */
        initpubsub();
        auto pipe0(fd_t::pipe().fatal("pipe0"));
        auto pipe1(fd_t::pipe().fatal("pipe1"));
        subscriber sub;
        iosubscription ss0(sub, pipe0.read.poll(POLLIN));
        iosubscription ss1(sub, pipe1.read.poll(POLLIN));
        (timestamp::now() + timedelta::milliseconds(50)).sleep(_io);
        assert(sub.poll() == NULL);
        (timestamp::now() + timedelta::milliseconds(50)).sleep(_io);
        pipe1.read.close();
        (timestamp::now() + timedelta::milliseconds(50)).sleep(_io);
        assert(sub.poll() == NULL);
        pipe0.write.write(_io, "foo", 3);
        (timestamp::now() + timedelta::milliseconds(50)).sleep(_io);
        auto s1(sub.poll());
        auto s2(sub.poll());
        assert(s1 != NULL);
        assert(s2 != NULL);
        assert(s1 != s2);
        assert(s1 == &ss1 || s1 == &ss0);
        assert(s2 == &ss1 || s2 == &ss0);
        pipe1.write.close();
        pipe0.close();
        deinitpubsub(_io); },
    "listenboth", [] {
        /* Basic tests when we're listening for read and write on the
           same FD in different subscriptions. */
        initpubsub();
        auto pipe(socket_t::socketpair().fatal("socketpair"));
        /* Start by filling the socket's send buffer. */
        while (1) {
            char b[4096];
            memset(b, 0, sizeof(b));
            auto r(pipe.fd0.write(
                       _io,
                       b,
                       sizeof(b),
                       timestamp::now() + timedelta::milliseconds(1)));
            if (r == error::timeout) break;
            r.fatal("filling sockbuf"); }
        {   subscriber sub;
            iosubscription reading(sub, pipe.fd0.poll(POLLIN));
            iosubscription writing(sub, pipe.fd0.poll(POLLOUT));
            (timestamp::now() + timedelta::milliseconds(100)).sleep(_io);
            assert(sub.poll() == NULL);
            {   char b[4096];
                pipe.fd1.readpoll(b, sizeof(b)).fatal("readpoll"); }
            (timestamp::now() + timedelta::milliseconds(100)).sleep(_io);
            assert(sub.poll() == &writing); }
        while (1) {
            char b[4096];
            memset(b, 0, sizeof(b));
            auto r(pipe.fd0.write(
                       _io,
                       b,
                       sizeof(b),
                       timestamp::now() + timedelta::milliseconds(1)));
            if (r == error::timeout) break;
            r.fatal("filling sockbuf"); }
        {   subscriber sub;
            iosubscription writing(sub, pipe.fd0.poll(POLLOUT));
            iosubscription reading(sub, pipe.fd0.poll(POLLIN));
            (timestamp::now() + timedelta::milliseconds(100)).sleep(_io);
            assert(sub.poll() == NULL);
            {   char b[4096];
                pipe.fd1.readpoll(b, sizeof(b)).fatal("readpoll"); }
            (timestamp::now() + timedelta::milliseconds(100)).sleep(_io);
            assert(sub.poll() == &writing); }
        deinitpubsub(_io); });
