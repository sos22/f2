#ifndef POLLABLE_COND_H__
#define POLLABLE_COND_H__

#include "fd.H"
#include "maybe.H"
#include "mutex.H"
#include "orerror.H"

/* A box you can put a value in, and some gubbins to wait until
   someone puts a value in it.  */
/* Note that each of these will consume two file descriptors, so you
   can't go creating them willy-nilly.  If you don't need something
   you can poll on then use a condition variable instead. */
template <typename t>
class waitbox {
    mutex_t mux;
    unsigned char content[sizeof(t)];
    bool filled;
    fd_t readfd;
    fd_t writefd;
    waitbox() {};
public:
    static orerror<waitbox *> build();
    void destroy();

    /* Set the waitbox.  Must be called at most once. */
    void set(const t &what);
    /* Check whether the waitbox is set, returning its contents if
       set. */
    maybe<t> poll() const;
    /* Wait for the waitbox to be set and return its contents. */
    const t &get() const;
    /* Check whether the waitbox is set, returning true if it is and
       false otherwise. */
    bool ready() const;
    /* Return an FD which will become readable once the waitbox is
       set, and remain so until the waitbox is destroyed. */
    const fd_t &fd() const;
};

#endif
