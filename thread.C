#include "thread.H"

#include <sys/prctl.h>
#include <stdlib.h>
#include <string.h>

#include "error.H"
#include "fields.H"
#include "util.H"

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

    ths->func->run(clientio::CLIENTIO);

    storerelease(&ths->finished_, true);
    ths->pub_.publish();
    return NULL;
}

thread::thread()
    : thr(),
      func(NULL),
      tid_(Nothing),
      startmux(),
      startcond(startmux),
      pub_(),
      finished_(false),
      pub(pub_) {}

orerror<void>
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
        /* Need to wait for a little bit to get the child thread's
           tid, because pthreads doesn't give us any easy way of doing
           so from the parent. */
        auto token(work->startmux.lock());
        while (work->tid_ == Nothing)
            token = work->startcond.wait(&token);
        work->startmux.unlock(&token);
        return Success;
    }
}

void
thread::join(clientio)
{
    pthread_join(thr, NULL);
    free((void *)name);
    delete this;
}

bool
thread::finished() const {
    return loadacquire(finished_); }

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
