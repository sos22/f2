#include <arpa/inet.h>
#include <unistd.h>

#include <valgrind/valgrind.h>

#include "logging.H"
#include "pubsub.H"
#include "socket.H"
#include "spark.H"
#include "test.H"
#include "testassert.H"
#include "test2.H"
#include "timedelta.H"

#include "orerror.tmpl"
#include "pair.tmpl"
#include "spark.tmpl"
#include "test.tmpl"
#include "testassert.tmpl"
#include "test2.tmpl"
#include "timedelta.tmpl"

static const auto epsilon(50_ms);
static const auto _io(clientio::CLIENTIO);

static testmodule __testpubsub(
    "pubsub",
    list<filename>::mk("pubsub.C", "pubsub.H"),
    testmodule::LineCoverage(95_pc),
    testmodule::BranchCoverage(68_pc),
    /* None of these take clientio tokens, because clientio tests
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
        assert(s.poll() == &a);
        assert(s.wait(_io, epsilon.future()) == NULL); },
    "nonconcurrentnotification", [] {
        publisher p;
        subscriber s;
        subscription a(s, p);
        assert(s.poll() == &a);
        assert(s.poll() == NULL);
        p.publish();
        assert(s.wait(_io, timestamp::now()) == &a);
        assert(s.wait(_io, epsilon.future()) == NULL); },
    "multinonconcurrentnotification", [] {
        publisher p;
        subscriber s;
        subscription a(s, p);
        assert(s.poll() == &a);
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
    "notifymulti", [] {
        publisher p1;
        publisher p2;
        subscriber s;
        subscription s1(s, p1);
        subscription s2(s, p2);
        while (s.poll() != NULL) ;
        p1.publish();
        p2.publish();
        auto ss1(s.poll());
        auto ss2(s.poll());
        assert(ss1 != ss2);
        assert(ss1 != NULL);
        assert(ss2 != NULL);
        assert(ss1 == &s1 || ss1 == &s2);
        assert(ss2 == &s1 || ss2 == &s2); },
    "2pub1sub", [] {
        publisher p1;
        publisher p2;
        subscriber s;
        subscription a(s, p1);
        assert(s.poll() == &a);
        assert(s.poll() == NULL);
        subscription b(s, p2);
        assert(s.poll() == &b);
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
        timestamp deadline((200_ms).future());
        int cntr;
        cntr = 0;
        spark<bool> t1([&pub, deadline] () {
                while (timestamp::now() < deadline) {
                    pub.publish();
                    if (running_on_valgrind()) sched_yield(); }
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
        tassert(T(cntr) >= T(3000)); },
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
                timedelta longest(0_s);
                while (timestamp::now() < deadline) {
                    auto tt(timedelta::time(
                                [&thread1, &ss, &sub, deadline] () {
                                    while (!thread1) {
                                        auto r(sub.wait(_io, deadline));
                                        assert(
                                            r == &ss ||
                                            timestamp::now() >= deadline); }}));
                    if (tt > longest) longest = tt;
                    assert(tt < 200_ms);
                    assert(thread1);
                    thread1 = false;
                    pub2.publish();
                    cntr1++; }
                logmsg(loglevel::debug, "longest stall " + longest.field()); });
        spark<void> t2([&thread1, &pub1, &pub2, &cntr2, deadline] {
                subscriber sub;
                subscription ss(sub, pub2);
                while (timestamp::now() < deadline) {
                    assert(timedelta::time(
                               [&thread1, &ss, &sub, deadline] () {
                                   while (thread1) {
                                       auto r(sub.wait(_io, deadline));
                                       assert(
                                           r == &ss ||
                                           timestamp::now() >= deadline); } })
                           < 200_ms);
                    assert(!thread1);
                    thread1 = true;
                    pub1.publish();
                    cntr2++; }
                logmsg(loglevel::debug, "t2 finished"); });
        t1.get();
        t2.get();
        tassert(T(cntr1) - T(cntr2) >= T(-1) && T(cntr1) - T(cntr2) <= T(1));
        tassert(T(cntr1) >= T(5000)); },
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
        tassert(T(t.first()) < T(closeread));
        tassert(T(closewrite) < T(t.second()));
        deinitpubsub(_io); },
    "listenclosed2", [] {
        /* listen on fd0 and fd1 at the same time, close fd1,
           confirm that making fd0 readable unblocks the stall. */
        initpubsub();
        auto pipe0(fd_t::pipe().fatal("pipe0"));
        auto pipe1(fd_t::pipe().fatal("pipe1"));
        subscriber sub;
        {   iosubscription ss0(sub, pipe0.read.poll(POLLIN));
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
            assert(s2 == &ss1 || s2 == &ss0); }
        pipe1.write.close();
        pipe0.close();
        deinitpubsub(_io); },
    "listenboth", [] {
        auto stall(1_ms);
        auto delay(100_ms);
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
                       stall.future()));
            if (r == error::timeout) break;
            r.fatal("filling sockbuf"); }
        {   subscriber sub;
            iosubscription reading(sub, pipe.fd0.poll(POLLIN));
            iosubscription writing(sub, pipe.fd0.poll(POLLOUT));
            delay.future().sleep(_io);
            assert(sub.poll() == NULL);
            {   char b[4096];
                pipe.fd1.readpoll(b, sizeof(b)).fatal("readpoll1"); }
            delay.future().sleep(_io);
            assert(sub.poll() == &writing); }
        while (1) {
            char b[4096];
            memset(b, 0, sizeof(b));
            auto r(pipe.fd0.write(
                       _io,
                       b,
                       sizeof(b),
                       stall.future()));
            if (r == error::timeout) break;
            r.fatal("filling sockbuf"); }
        {   subscriber sub;
            iosubscription writing(sub, pipe.fd0.poll(POLLOUT));
            iosubscription reading(sub, pipe.fd0.poll(POLLIN));
            delay.future().sleep(_io);
            assert(sub.poll() == NULL);
            {   char b[4096];
                pipe.fd1.readpoll(b, sizeof(b)).fatal("readpoll2"); }
            delay.future().sleep(_io);
            assert(sub.poll() == &writing); }
        deinitpubsub(_io); },
#if TESTING
    "publots", [] {
        /* A single subscriber which subscribes to lots of publishers
         * should produce some warning messages (and also work). */
        initpubsub();
        const unsigned nrpubs = 2000;
        {   publisher pubs[nrpubs];
            subscriber sub;
            auto ss = (subscription *)calloc(nrpubs, sizeof(subscriber));
            for (auto x = 0; x < nrpubs; x++) {
                new (&ss[x]) subscription(sub, pubs[x]); }
            unsigned nrmsgs = 0;
            tests::eventwaiter< ::loglevel> waiter(
                tests::logmsg,
                [&nrmsgs] (loglevel level) {
                    if (level >= loglevel::debug) nrmsgs++; });
            for (auto x = 0; x < nrpubs; x++) assert(sub.wait(_io) != NULL);
            assert(nrmsgs > 0);
            nrmsgs = 0;
            for (auto x = 0; x < nrpubs; x++) {
                pubs[x].publish();
                assert(sub.poll() == &ss[x]); }
            assert(nrmsgs > 0);
            for (auto x = 0; x < nrpubs; x++) ss[x].~subscription();
            free(ss); }
        deinitpubsub(_io); },
    "sublots", [] {
        /* Lots of subscribers which all subscribe to the same
         * publisher. */
        initpubsub();
        const unsigned nrsubs = 20000;
        {   publisher pub;
            subscriber subs[nrsubs];
            auto ss = (subscription *)calloc(nrsubs, sizeof(subscriber));
            for (auto x = 0; x < nrsubs; x++) {
                new (&ss[x]) subscription(subs[x], pub); }
            unsigned nrmsgs = 0;
            tests::eventwaiter< ::loglevel> waiter(
                tests::logmsg,
                [&nrmsgs] (loglevel level) {
                    if (level >= loglevel::debug) nrmsgs++; });
            for (auto x = 0; x < nrsubs; x++) assert(subs[x].wait(_io)==&ss[x]);
            nrmsgs = 0;
            pub.publish();
            assert(nrmsgs > 0);
            for (auto x = 0; x < nrsubs; x++) assert(subs[x].wait(_io)==&ss[x]);
            for (auto x = 0; x < nrsubs; x++) ss[x].~subscription();
            free(ss); }
        deinitpubsub(_io); },
#endif
    "immediateunpoll", [] (clientio io) {
        /* Closing an iosubscription should wait until the iowait
         * thread has finished waiting for it, so that closing a
         * listening socket and then creating a new one works as
         * expected. */
        /* Run it a bunch of times to be more likely to cover
         * all thread schedulings. */
        for (int cntr = 0; cntr < 100; cntr++) {
            int fd(::socket(AF_INET, SOCK_STREAM, 0));
            if (fd < 0) error::from_errno().fatal("socket");
            if (::listen(fd, 10) < 0) error::from_errno().fatal("listen");
            sockaddr_in sin;
            socklen_t ss = sizeof(sin);
            if (::getsockname(fd, (sockaddr *)&sin, &ss) < 0) {
                error::from_errno().fatal("getsockname"); }
            int fd2(::socket(AF_INET, SOCK_STREAM, 0));
            if (fd2 < 0) error::from_errno().fatal("socket");
            subscriber sub;
            {   struct pollfd pfd;
                pfd.fd = fd;
                pfd.events = POLLIN;
                pfd.revents = 0;
                iosubscription ios(sub, pfd);
                /* A little bit of a stall makes it more likely that
                 * the thread will have actually noticed the iosub
                 * before we release it. */
                (10_ms).future().sleep(io); }
            iosubscription::synchronise(io);
            ::close(fd);
            if (::bind(fd2, (sockaddr *)&sin, ss) < 0) {
                error::from_errno().fatal("bind2"); }
            ::close(fd2); } },
    testmodule::TestFlags::noauto(), "condpingpong", [] {
        auto takesample([] (clientio io) {
                pthread_mutex_t mux;
                pthread_mutex_init(&mux, NULL);
                pthread_cond_t cond;
                pthread_cond_init(&cond, NULL);
                unsigned long cntr = 0;
                bool done = false;
                unsigned phase = 0;
                unsigned long start;
                unsigned long end;
                {   spark<void> thr1([&] {
                            assert(phase == 0);
                            pthread_mutex_lock(&mux);
                            while (!done) {
                                cntr++;
                                phase = 1;
                                pthread_cond_signal(&cond);
                                while (phase == 1 && !done) {
                                    pthread_cond_wait(&cond, &mux); } }
                            pthread_mutex_unlock(&mux); });
                    spark<void> thr2([&] {
                            pthread_mutex_lock(&mux);
                            while (!done) {
                                while (phase != 1 && !done) {
                                    pthread_cond_wait(&cond, &mux); }
                                phase = 2;
                                pthread_cond_signal(&cond); }
                            pthread_mutex_unlock(&mux); });
                    /* Give it a second to get started. */
                    (1_s).future().sleep(io);
                    start = __sync_fetch_and_add(&cntr, 0);
                    /* And then let it run for a bit. */
                    (10_s).future().sleep(io);
                    end = __sync_fetch_and_add(&cntr, 0);
                    done = true;
                    pthread_cond_broadcast(&cond); }
                pthread_cond_destroy(&cond);
                pthread_mutex_destroy(&mux);
                return end - start; });
        list<unsigned long> samples;
        for (unsigned cntr = 0; cntr < 5; cntr++) {
            auto s(takesample(clientio::CLIENTIO));
            logmsg(loglevel::info, "sample " + fields::mk(s));
            samples.pushtail(s); } },
    testmodule::TestFlags::noauto(), "pubsubpingpong", [] {
        auto takesample([] (clientio io) {
                unsigned long cntr = 0;
                bool done = false;
                unsigned phase = 0;
                publisher phase1;
                publisher phase2;
                unsigned long start;
                unsigned long end;
                {   spark<void> thr1([&] {
                            assert(phase == 0);
                            subscriber sub;
                            subscription ss(sub, phase2);
                            while (!done) {
                                cntr++;
                                phase = 1;
                                phase1.publish();
                                while (phase == 1 && !done) {
                                    sub.wait(clientio::CLIENTIO); } } } );
                    spark<void> thr2([&] {
                            subscriber sub;
                            subscription ss(sub, phase1);
                            while (!done) {
                                while (phase != 1 && !done) {
                                    sub.wait(clientio::CLIENTIO); }
                                phase = 2;
                                phase2.publish(); } } );
                    /* Give it a second to get started. */
                    (1_s).future().sleep(io);
                    start = __sync_fetch_and_add(&cntr, 0);
                    /* And then let it run for a bit. */
                    (10_s).future().sleep(io);
                    end = __sync_fetch_and_add(&cntr, 0);
                    done = true;
                    phase1.publish();
                    phase2.publish(); }
                return end - start; });
        list<unsigned long> samples;
        for (unsigned cntr = 0; cntr < 5; cntr++) {
            auto s(takesample(clientio::CLIENTIO));
            logmsg(loglevel::info, "sample " + fields::mk(s));
            samples.pushtail(s); } },
    "fields", [] (clientio io) {
        publisher pub;
        assert(!strcmp(pub.field().c_str(), "<publisher: <unheld>>"));
        subscriber sub;
        subscription ss(sub, pub, (void *)0x1234);
        auto s(ss.field().c_str());
        logmsg(loglevel::debug, s);
        assert(strstr(s, "data 1234"));
        assert(strstr(s, "notified TRUE"));
        assert(sub.poll() == &ss);
        s = ss.field().c_str();
        logmsg(loglevel::debug, s);
        assert(strstr(s, "data 1234"));
        assert(strstr(s, "notified FALSE"));
        auto p(fd_t::pipe().fatal("pipe"));
        iosubscription ios(sub, p.read.poll(POLLIN));
        s = ios.field().c_str();
        logmsg(loglevel::debug, s);
        assert(memcmp(s, "ios", 3) == 0);
        assert(strstr(s, "notified FALSE"));
        assert(strstr(s, "registered TRUE"));
        p.write.write(io, "XXX", 3).fatal("write");
        (200_ms).future().sleep(io);
        s = ios.field().c_str();
        logmsg(loglevel::debug, s);
        assert(strstr(s, "notified TRUE"));
        assert(strstr(s, "registered FALSE"));
        ios.rearm();
        (200_ms).future().sleep(io);
        s = ios.field().c_str();
        logmsg(loglevel::debug, s);
        assert(strstr(s, "notified TRUE"));
        assert(strstr(s, "registered FALSE"));
        {   char buf[3];
            p.read.read(io, buf, 3).fatal("read"); }
        ios.rearm();
        (200_ms).future().sleep(io);
        s = ios.field().c_str();
        logmsg(loglevel::debug, s);
        assert(strstr(s, "notified TRUE"));
        assert(strstr(s, "registered FALSE"));
        p.close(); },
    "fields2", [] {
        subscriber sub;
        auto s(sub.field().c_str());
        logmsg(loglevel::debug, s);
        assert(strstr(s, "nrnotified 0"));
        publisher pub;
        subscription ss(sub, pub, (void *)0x5678);
        s = sub.field().c_str();
        logmsg(loglevel::debug, s);
        assert(strstr(s, "nrnotified 1"));
        assert(strstr(s, "data 5678"));
        assert(sub.poll() == &ss);
        s = sub.field().c_str();
        logmsg(loglevel::debug, s);
        assert(strstr(s, "nrnotified 0")); }
    );
