#ifndef BUFFER_H__
#define BUFFER_H__

#include "error.H"
#include "list.H"
#include "maybe.H"

class fd_t;

class buffer {
    class subbuf {
    public:
	subbuf *next;
	unsigned sz;
	size_t prod;
	size_t cons;
	unsigned char payload[];
    };
    subbuf *first;
    subbuf *last;
    /* How many bytes have we produced in this buffer, ever? */
    size_t prod;
    /* How many bytes have we removed from this buffer, ever? */
    size_t cons;

    subbuf *newsubbuf(size_t, bool, bool, bool);
public:
    buffer() : first(), last(), prod(0), cons(0) {}
    ~buffer();
    maybe<error> send(fd_t);
    maybe<error> receive(fd_t);
    void queue(const void *, size_t sz);
    void fetch(void *buf, size_t sz);
    void pushback(const void *, size_t sz);
    bool empty() const;
    size_t avail() const;
    size_t offset() const;
    void discard(size_t amt);
    char idx(size_t) const;
    void *linearise(size_t start, size_t end);
};

#endif /* !BUFFER_H__ */
