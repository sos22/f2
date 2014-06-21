#include "pubsub.H"

#include "test.H"
#include "timedelta.H"

#include "list.tmpl"

publisher::publisher()
    : mux(),
      subscriptions() {}

void
publisher::publish() {
    auto tok(mux.lock());
    for (auto it(subscriptions.start()); !it.finished(); it.next()) {
        (*it)->set(); }
    mux.unlock(&tok); }

publisher::~publisher() {}

subscription::subscription(subscriber &_sub, publisher &_pub)
    : notified(false),
      sub(&_sub),
      pub(&_pub) {
    auto subtoken(sub->mux.lock());
    sub->subscriptions.pushtail(this);
    sub->mux.unlock(&subtoken);
    
    auto pubtoken(pub->mux.lock());
    pub->subscriptions.pushtail(this);
    pub->mux.unlock(&pubtoken); }

void
subscription::set() {
    assert(sub);
    assert(pub);
    if (!notified) {
        auto tok(sub->mux.lock());
        notified = true;
        sub->set(tok);
        sub->mux.unlock(&tok); } }

subscription::~subscription() {
    assert(!!pub == !!sub);
    if (!pub) return; /* subscriber destructor already did everything
                       * for us */
    bool r;
    r = false;
    auto pubtoken(pub->mux.lock());
    for (auto it(pub->subscriptions.start()); !it.finished(); it.next()) {
        if (*it == this) {
            it.remove();
            r = true;
            break; } }
    pub->mux.unlock(&pubtoken);
    assert(r);
    
    r = false;
    auto subtoken(sub->mux.lock());
    for (auto it(sub->subscriptions.start()); !it.finished(); it.next()) {
        if (*it == this) {
            it.remove();
            r = true;
            break; } }
    sub->mux.unlock(&subtoken);
    assert(r); }

void
subscriber::set(mutex_t::token tok) {
    if (!notified) {
        notified = true;
        cond.broadcast(tok); } }

subscriber::subscriber()
    : mux(),
      cond(mux),
      notified(false),
      subscriptions() {}

subscription *
subscriber::wait(maybe<timestamp> deadline) {
    auto token(mux.lock());
    while (1) {
        notified = false;
        for (auto it(subscriptions.start()); !it.finished(); it.next()) {
            auto r(*it);
            assert(r->sub == this);
            if (r->notified) {
                r->notified = false;
                mux.unlock(&token);
                return r; } }
        while (!notified) {
            auto r(cond.wait(&token, deadline));
            token = r.token;
            if (r.timedout) {
                mux.unlock(&token);
                return NULL; } } } }

subscriber::~subscriber() {
    while (!subscriptions.empty()) {
        auto r(subscriptions.pophead());
        assert(r->sub == this);
        assert(r->pub);
        
        auto pubtoken(r->pub->mux.lock());
        bool found = false;
        for (auto it(r->pub->subscriptions.start());
             !found && !it.finished();
             it.next()) {
            if (*it == r) {
                it.remove();
                found = true; } }
        r->pub->mux.unlock(&pubtoken);
        assert(found);
        
        r->pub = NULL;
        r->sub = NULL; } }

template class list<subscription *>;

void
tests::pubsub(test &support) {
    auto epsilon(timedelta::milliseconds(10));
    support.msg("Publisher in isolation");
    support.detail("construct/destruct");
    { publisher p; }
    support.detail("empty publish");
    { publisher().publish(); }
    support.msg("Subscriber in isolation");
    support.detail("construct/destruct");
    { subscriber s; }
    support.detail("Empty wait times out");
    assert(subscriber().wait(timestamp::now()) == NULL);
    assert(subscriber().wait(timestamp::now() + epsilon) == NULL);
    support.msg("Cross-class tests");
    support.detail("Basic sub/unsub");
    {   publisher p;
        subscriber s;
        subscription a(s, p);
        assert(s.wait(timestamp::now() + epsilon) == NULL); }
    support.detail("Non-concurrent notifications");
    {   publisher p;
        subscriber s;
        subscription a(s, p);
        p.publish();
        assert(s.wait(timestamp::now()) == &a);
        assert(s.wait(timestamp::now() + epsilon) == NULL); }
    support.detail("Multiple non-concurrent notifications");
    {   publisher p;
        subscriber s;
        subscription a(s, p);
        p.publish();
        p.publish();
        assert(s.wait(timestamp::now()) == &a);
        assert(s.wait(timestamp::now() + epsilon) == NULL);
        p.publish();
        assert(s.wait(timestamp::now()) == &a);
        assert(s.wait(timestamp::now() + epsilon) == NULL); }
    support.detail("Notification after unsubscribe");
    {   publisher p;
        subscriber s;
        {   subscription a(s, p);
            p.publish();
            assert(s.wait(timestamp::now()) == &a); }
        assert(s.wait(timestamp::now() + epsilon) == NULL);
        p.publish();
        assert(s.wait(timestamp::now() + epsilon) == NULL); }
    support.detail("One publisher, one subscriber, multiple subscriptions");
    {   publisher p;
        subscriber s;
        {   subscription a(s, p);
            {   subscription b(s, p);
                p.publish();
                auto f(s.wait(timestamp::now()));
                auto g(s.wait(timestamp::now()));
                auto h(s.wait(timestamp::now() + epsilon));
                assert(f != g);
                assert(f == &a || f == &b);
                assert(g == &a || g == &b);
                assert(h == NULL); }
            assert(s.wait(timestamp::now() + epsilon) == NULL);
            p.publish();
            assert(s.wait(timestamp::now()) == &a);
            assert(s.wait(timestamp::now() + epsilon) == NULL); }
        p.publish();
        assert(s.wait(timestamp::now() + epsilon) == NULL); }
    support.detail("Same, but unsubscribe while notified.");
    {   publisher p;
        subscriber s;
        {   subscription a(s, p);
            {   subscription b(s, p);
                p.publish(); }
            assert(s.wait(timestamp::now()) == &a);
            assert(s.wait(timestamp::now() + epsilon) == NULL);
            p.publish();
            assert(s.wait(timestamp::now()) == &a);
            assert(s.wait(timestamp::now() + epsilon) == NULL); } }
    support.detail("One publisher, two subscribers");
    {   publisher p;
        subscriber s1;
        subscription a(s1, p);
        {   subscriber s2;
            subscription b(s2, p);
            p.publish();
            assert(s1.wait(timestamp::now()) == &a);
            assert(s1.wait(timestamp::now() + epsilon) == NULL);
            assert(s2.wait(timestamp::now()) == &b);
            assert(s2.wait(timestamp::now() + epsilon) == NULL); }
        p.publish();
        assert(s1.wait(timestamp::now()) == &a);
        assert(s1.wait(timestamp::now() + epsilon) == NULL); }
    support.detail("Two publishers, one subscriber");
    {   publisher p1;
        publisher p2;
        subscriber s;
        subscription a(s, p1);
        subscription b(s, p2);
        assert(s.wait(timestamp::now() + epsilon) == NULL);
        p1.publish();
        assert(s.wait(timestamp::now()) == &a);
        assert(s.wait(timestamp::now() + epsilon) == NULL);
        p2.publish();
        assert(s.wait(timestamp::now()) == &b);
        assert(s.wait(timestamp::now() + epsilon) == NULL);
        p1.publish();
        p2.publish();
        auto f(s.wait(timestamp::now()));
        auto g(s.wait(timestamp::now()));
        auto h(s.wait(timestamp::now() + epsilon));
        assert(f != g);
        assert(f == &a || f == &b);
        assert(g == &a || g == &b);
        assert(h == NULL); } }
