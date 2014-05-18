#ifndef WAITQUEUE_H__
#define WAITQUEUE_H__

#include "list.H"

class fd_t;

template <typename t>
class waitqueue {
    mutex_t mux;
    fd_t readfd;
    fd_t writefd;
    list<t> content;
public:
    t pop();
    void push(const t &);
    fd_t fd() const;
    void destroy() const;
    static orerror<waitqueue *> build();
};

#endif /* !WAITQUEUE_H__ */
