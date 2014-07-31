#ifndef POLLABLE_COND_H__
#define POLLABLE_COND_H__

#include "maybe.H"
#include "mutex.H"
#include "pubsub.H"

class clientio;

/* A box you can put a value in, and some gubbins to wait until
   someone puts a value in it.  */
template <typename t>
class waitbox {
    mutable mutex_t mux; /* mutable because we need to acquire it from
                          * the const ready() method. */
    maybe<t> content;
    publisher pub_;
public:  waitbox() : mux(), content(Nothing), pub(pub_) {};
public:  const publisher &pub;

    /* Set the waitbox.  Must be called at most once. */
    void set(const t &what);
    /* Wait for the waitbox to be set and return its contents. */
    const t &get(clientio) const;
    /* Check whether the waitbox is set, returning true if it is and
       false otherwise. */
    bool ready() const;
};

#endif
