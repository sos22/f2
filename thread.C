#include "thread.H"

#include <sys/prctl.h>
#include <signal.h>

#include "error.H"
#include "fields.H"
#include "profile.H"
#include "test.H"
#include "tid.H"
#include "timedelta.H"
#include "util.H"

#include "list.tmpl"
#include "orerror.tmpl"
#include "thread.tmpl"
#include "waitbox.tmpl"

static __thread const char *this_name;
static __thread thread *this_thread;
static unsigned nrthreads;

thread::thread(const thread::constoken &token)
    : thr(),
      started(false),
      tid_(),
      name(strdup(token.name.c_str())),
      dead(false) {}

void
thread::go() {
    assert(!started);
    started = true;
    int r = pthread_create(&thr,
                           NULL,
                           pthreadstart,
                           this);
    if (r) error::from_errno(r).fatal("spawn thread " + fields::mk(name));
    atomicinc(::nrthreads); }

void *
thread::pthreadstart(void *_this) {
    thread *thr = static_cast<thread *>(_this);
    assert(thr->started == true);
    assert(this_thread == NULL);
    this_thread = thr;
    /* pthread API doesn't give a good way of getting tid from parent
       process, so do it from here instead. */
    thr->tid_.set(tid::me());
    this_name = thr->name;
    prctl(PR_SET_NAME, (unsigned long)this_name, 0, 0);
    profilenewthread();
    thr->run(clientio::CLIENTIO);
    profileendthread();
    /* Tell subscribers that we died. */
    thr->dead.storerelease(true);
    thr->_pub.publish();
    atomicloaddec(nrthreads);
    this_thread = NULL;
    return NULL; }

orerror<void>
thread::kill(int signal) {
    if (::pthread_kill(thr, signal) < 0) return error::from_errno();
    else return Success; }

maybe<thread::deathtoken>
thread::hasdied() const {
    if (dead.loadacquire()) return deathtoken();
    else return Nothing; }

const publisher &
thread::pub() const { return _pub; }

void
thread::join(deathtoken) {
    /* We guarantee that the thread shuts down quickly once dead is
     * set, and that no death tokens can be created until dead is set,
     * so this is guaranteed to finish quickly. */
    assert(dead.load());
    int r = pthread_join(thr, NULL);
    if (r) error::from_errno(r).fatal("joining thread " + fields::mk(name));
    delete this; }

void
thread::join(clientio io) {
    if (!started) {
        delete this;
        return; }
    auto token(hasdied());
    if (token == Nothing) {
        subscriber sub;
        subscription ds(sub, pub());
        token = hasdied();
        while (token == Nothing) {
            auto d(sub.wait(io));
            assert(d == &ds);
            token = hasdied(); } }
    join(token.just()); }

thread::~thread() { free((void *)name); }

const fields::field &
thread::field() const {
    const fields::field *acc(&fields::mk("<thread:"));
    auto tid(tid_.poll());
    if (tid.isjust()) acc = &(*acc + fields::mk(tid.just()));
    else acc = &(*acc + fields::mk("{no tid yet}"));
    acc = &(*acc + " " + fields::mk(name));
    if (dead.load()) acc = &(*acc + " dead");
    if (!started) acc = &(*acc + " unstarted");
    return *acc + ">"; }

const char *
thread::myname() { return this_name ?: "<unknown thread>"; }

void
thread::initialthread() { this_name = "main"; }

thread *
thread::me() { return this_thread; }

namespace {
class exitchecks {
public: static void doit(int code, void *) {
    if (code == 0) assert(nrthreads == 0); }
public: exitchecks() { on_exit(doit, NULL); } };
static exitchecks _exitchecks; }
