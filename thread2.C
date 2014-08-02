#include "thread2.H"

#include <sys/prctl.h>

#include "error.H"
#include "fields.H"
#include "tid.H"
#include "util.H"

#include "list.tmpl"
#include "waitbox.tmpl"

static mutex_t
detachlock;

thread2::thread2(thread2::constoken token)
    : thr(),
      started(false),
      tid_(),
      name(strdup(token.name.c_str())),
      dead(false),
      subscribers() {}

void
thread2::go() {
    assert(!started);
    started = true;
    if (pthread_create(&thr,
                       NULL,
                       pthreadstart,
                       this) != 0) {
        error::from_errno().fatal("spawn thread " + fields::mk(name)); } }

void *
thread2::pthreadstart(void *_this) {
    thread2 *thr = static_cast<thread2 *>(_this);
    assert(thr->started == true);
    /* pthread API doesn't give a good way of getting tid from parent
       process, so do it from here instead. */
    thr->tid_.set(tid::me());
    prctl(PR_SET_NAME, (unsigned long)thr->name, 0, 0);
    thr->run(clientio::CLIENTIO);
    /* Tell subscribers that we died. */
    auto token(detachlock.lock());
    storerelease(&thr->dead, true);
    for (auto it(thr->subscribers.start()); !it.finished(); it.remove()) {
        (*it)->owner = NULL;
        (*it)->set(); }
    detachlock.unlock(&token);
    return NULL; }

maybe<thread2::deathtoken>
thread2::hasdied() const {
    if (loadacquire(dead)) return deathtoken();
    else return Nothing; }

thread2::deathsubscription::deathsubscription(subscriber &_sub,
                                              thread2 *_owner)
    : subscriptionbase(_sub),
      owner(_owner) {
    auto token(detachlock.lock());
    owner->subscribers.pushtail(this);
    detachlock.unlock(&token);
    if (loadacquire(_owner->dead)) set(); }

void
thread2::deathsubscription::detach() {
    auto token(detachlock.lock());
    if (owner) {
        bool found = false;
        for (auto it(owner->subscribers.start()); !it.finished(); it.next()) {
            if (*it == this) {
                it.remove();
                found = true;
                break; } }
        assert(found);
        owner = NULL; }
    detachlock.unlock(&token); }

thread2::deathsubscription::~deathsubscription() {
    detach(); }

void
thread2::join(deathtoken) {
    assert(loadacquire(dead));
    if (pthread_join(thr, NULL) < 0) {
        error::from_errno().fatal("joining thread " + fields::mk(name)); }
    auto token(detachlock.lock());
    while (!subscribers.empty()) {
        auto i = subscribers.pophead();
        if (i->owner == NULL) continue;
        assert(i->owner == this);
        i->owner = NULL; }
    detachlock.unlock(&token);
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
