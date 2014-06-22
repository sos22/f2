#ifndef WAITQUEUE_H__
#define WAITQUEUE_H__

#include "list.H"
#include "pubsub.H"

class publisher;

/* A list extended with something to wait until it's non-empty. */
template <typename t>
class waitqueue {
    mutable mutex_t mux;
    publisher pub_;
    list<t> content;
public:  waitqueue()
    : mux(),
      pub_(),
      content(),
      pub(pub_) {}
public:
    const publisher &pub;
    t pophead();
    void pushtail(const t &);
    /* Destruct the queue.  It must be empty before calling this
     * (checked by content destructor).  */
public:  ~waitqueue() {}
    /* Check whether the queue is currently empty */
    bool empty() const;
};

#endif /* !WAITQUEUE_H__ */
