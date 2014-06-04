#ifndef WAITQUEUE_H__
#define WAITQUEUE_H__

#include "list.H"

class fd_t;

/* Like a list, except that it has an associated FD which you can poll
 * on. */
/* Much like a waitbox, each of these will consume two FDs, so don't
 * use them where a list and a cond var would suffice. */
template <typename t>
class waitqueue {
    mutex_t mux;
    fd_t readfd;
    fd_t writefd;
    list<t> content;
public:
    t pophead();
    void pushtail(const t &);
    fd_t fd() const;
    /* Destroy the queue.  It must be empty before calling this. */
    void destroy() const;
    static orerror<waitqueue *> build();
};

#endif /* !WAITQUEUE_H__ */
