#include "thread.H"

#include <sys/prctl.h>
#include <stdlib.h>
#include <string.h>

#include "error.H"
#include "fields.H"

static pthread_mutex_t
destructorsmux;
static pthread_once_t
destructorsonce = PTHREAD_ONCE_INIT;
static threaddestructor *
headdestructor;
static void
destructorsinit() {
    pthread_mutex_init(&destructorsmux, NULL); }
static void
destructorslock() {
    pthread_once(&destructorsonce, destructorsinit);
    pthread_mutex_lock(&destructorsmux); }
static void
destructorsunlock() {
    pthread_mutex_unlock(&destructorsmux); }

void *
thread::startfn(void *_ths)
{
    thread *ths = (thread *)_ths;
    assert(ths->tid_ == Nothing);

    prctl(PR_SET_NAME, (unsigned long)ths->name, 0, 0);

    auto token(ths->startmux.lock());
    ths->tid_ = tid::me();
    ths->startcond.broadcast(token);
    ths->startmux.unlock(&token);

    ths->func->run();

    int nrdestructorsalloced = 0;
    threaddestructor **destructors = NULL;
    int nrdestructors = 0;
    destructorslock();
    for (auto it(headdestructor); it; it = it->next) {
        if (nrdestructorsalloced == nrdestructors) {
            nrdestructorsalloced += 16;
            destructors = (threaddestructor **)realloc(destructors,
                                                       sizeof(destructors[0]) *
                                                       nrdestructorsalloced);
        }
        it->start();
        destructors[nrdestructors++] = it;
    }
    destructorsunlock();
    for (int i = 0; i < nrdestructors; i++) {
        destructors[i]->die();
        destructors[i]->finished();
    }
    free(destructors);
    return NULL;
}

thread::thread()
    : thr(), func(NULL), tid_(Nothing), startmux(), startcond(startmux)
{}

maybe<error>
thread::spawn(threadfn *fn, thread **out, const fields::field &name)
{
    fields::fieldbuf buf;
    name.fmt(buf);
    thread *work = new thread();
    work->func = fn;
    work->name = strdup(buf.c_str());
    *out = work;
    int err = pthread_create(&work->thr, NULL, startfn, work);
    if (err) {
        *out = NULL;
        delete work;
        return error::from_errno(err);
    } else {
        auto token(work->startmux.lock());
        while (work->tid_ == Nothing)
            token = work->startcond.wait(&token);
        work->startmux.unlock(&token);
        return Nothing;
    }
}

void
thread::join()
{
    pthread_join(thr, NULL);
    free((void *)name);
    delete this;
}

threaddestructor::threaddestructor()
    : mux(),
      idle(mux),
      next(NULL),
      outstanding(0) {
    destructorslock();
    next = headdestructor;
    headdestructor = this;
    destructorsunlock(); }
threaddestructor::~threaddestructor() {
    destructorslock();
    threaddestructor **pprev;
    for (pprev = &headdestructor; *pprev != this; pprev = &(*pprev)->next)
        ;
    *pprev = next;
    destructorsunlock();
    auto token(mux.lock());
    while (outstanding) token = idle.wait(&token);
    mux.unlock(&token); }
void
threaddestructor::start() {
    auto token(mux.lock());
    outstanding++;
    mux.unlock(&token); }
void
threaddestructor::finished() {
    auto token(mux.lock());
    assert(outstanding);
    outstanding--;
    if (!outstanding) idle.broadcast(token);
    mux.unlock(&token); }

class threadfield : fields::field {
    const thread &t;
    void fmt(fields::fieldbuf &o) const
        {
            ("<" +
             fields::mk(t.name) +
             "|" +
             fields::mk(t.tid_.just()) +
             ">").fmt(o);
        }
    threadfield(const thread &_t)
        : t(_t)
        {}
public:
    static const fields::field &n(const thread &t)
        { return *new threadfield(t); }
};
const fields::field &
fields::mk(const thread &t)
{
    return threadfield::n(t);
}
