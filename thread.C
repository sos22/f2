#include "thread.H"

void *
thread::startfn(void *_ths)
{
    thread *ths = (thread *)_ths;
    ths->func->run();
    return NULL;
}


maybe<error>
thread::spawn(threadfn *fn, thread **out)
{
    thread *work = new thread();
    work->func = fn;
    *out = work;
    int err = pthread_create(&work->thr, NULL, startfn, work);
    if (err) {
	*out = NULL;
	delete work;
	return maybe<error>::mkjust(error::from_errno(err));
    } else {
	return maybe<error>::mknothing();
    }
}

void
thread::join()
{
    pthread_join(thr, NULL);
    delete this;
}
