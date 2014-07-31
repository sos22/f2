#include "thread2.H"

#include "error.H"
#include "fields.H"
#include "tid.H"
#include "util.H"

thread2::thread2()
    : thr(),
      tid_(),
      name(NULL),
      lock(),
      dead(false),
      subscribers() {}

void *
thread2::pthreadstart(void *_this) {
    thread2 *thr = static_cast<thread2 *>(_this);
    /* pthread API doesn't give a good way of getting tid from parent
       process, so do it from here instead. */
    thr->tid_.set(tid::me());
    thr->run(clientio::CLIENTIO);
    /* Tell subscribers that we died. */
    auto token(thr->lock.lock());
    storerelease(&thr->dead, true);
    for (auto it(thr->subscribers.start()); !it.finished(); it.remove()) {
        (*it)->set(); }
    thr->lock.unlock(&token);
    return NULL; }

maybe<thread2::deathtoken>
thread2::hasdied() const {
    if (loadacquire(dead)) return deathtoken();
    else return Nothing; }

thread2::deathsubscription::deathsubscription(subscriber &_sub,
                                              thread2 *_owner)
    : subscriptionbase(_sub),
      owner(_owner) {
    auto token(owner->lock.lock());
    owner->subscribers.pushtail(this);
    owner->lock.unlock(&token);
    if (loadacquire(owner->dead)) set(); }

void
thread2::deathsubscription::detach() {
    if (owner == NULL) return;
    auto token(owner->lock.lock());
    if (!owner->dead) {
        for (auto it(owner->subscribers.start()); !it.finished(); it.next()) {
            if (*it == this) {
                it.remove();
                break; } } }
    owner->lock.unlock(&token);
    owner = NULL; }

void
thread2::join(deathtoken) {
    assert(loadacquire(dead));
    /* subscribers list might be non-empty if someone attached after
     * we died.  Nobody should attach after join() has been called,
     * and the actual thread is dead, so we don't need the lock here.
     * Which is handy, because otherwise we'd deadlock calling
     * detach(). */
    while (!subscribers.empty()) {
        subscribers.peekhead()->detach(); }
    if (pthread_join(thr, NULL) < 0) {
        error::from_errno().fatal("joining thread " + fields::mk(name)); }
    delete this; }

void
thread2::join(clientio io) {
    subscriber sub;
    deathsubscription ds(sub, this);
    auto d(sub.wait(io));
    assert(d == &ds);
    auto token(hasdied());
    assert(token != Nothing);
    join(token.just()); }

thread2::~thread2() {
    assert(subscribers.empty());
    free((void *)name); }

const fields::field &
fields::mk(const thread2 &thr) {
    auto &prefix("<thread:" +
                 /* The new thread is guaranteed to populate tid soon
                    after starting, so we don't need a real clientio
                    token here. */
                 fields::mk(thr.tid_.get(clientio::CLIENTIO)) + " " +
                 fields::mk(thr.name));
    if (loadacquire(thr.dead)) return prefix + " dead>";
    else return prefix + ">"; }
