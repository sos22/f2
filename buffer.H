#ifndef BUFFER_H__
#define BUFFER_H__

#include <stddef.h>

#include "error.H"
#include "list.H"
#include "maybe.H"
#include "timestamp.H"

class fd_t;
template <typename> class orerror;
class subscriber;
class subscriptionbase;
class test;

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
    subbuf *extend_end(size_t sz = 0) { return newsubbuf(sz, false, false, true); }
    subbuf *extend_start(size_t sz) { return newsubbuf(sz, true, true, true); }
    subbuf *unlinked_subbuf(size_t sz) { return newsubbuf(sz, false, false, false); }
    buffer(const buffer &o) = delete;
    void operator=(const buffer &o) = delete;
public:
    buffer() : first(NULL), last(NULL), prod(0), cons(0) {}
    ~buffer();

    /* Interface to fd_t */
    /* Send some bytes from the start of the buffer.  Only ever makes
     * a single write syscall, so won't block if poll returns POLLOUT.
     * Might leave some bytes in the buffer, even when it returns
     * success. */
    maybe<error> send(fd_t, maybe<timestamp> deadline = Nothing);
    /* Grab some bytes from the fd and put them at the end of the
     * buffer.  Only ever makes a single read syscall, so won't block
     * if poll returns POLLIN. */
    maybe<error> receive(fd_t, maybe<timestamp> deadline = Nothing);
    /* Grab some bytes from the fd, waiting at most until the deadline
     * or until the subscriber becomes notified.  If the subscriber
     * becomes notified, clear and return the subscription.  If an
     * error occurs, return that error.  Otherwise, return NULL. */
    orerror<subscriptionbase *> receive(fd_t,
                                        subscriber &sub,
                                        maybe<timestamp> deadline = Nothing);

    /* Interface for setting up buffers from memory. */
    /* Copy some bytes from memory to the end of the buffer.  The
     * buffer is expanded as needed to make space. */
    void queue(const void *, size_t sz);
    /* Copy some bytes out of the front of the buffer, removing them
     * from the buffer as we do so.  It is an error to request more
     * bytes than are available.  Discards the bytes rather than
     * copying them if @buf is NULL. */
    void fetch(void *buf, size_t sz);
    /* Copy some bytes from memory to the start of the buffer.  The
     * buffer is expanded as needed to make space.  This is the
     * inverse of fetch(). */
    void pushback(const void *, size_t sz);

    /* Check whether a buffer is currently empty.  Equivalent to
     * avail() != 0, but often slightly more efficient. */
    bool empty() const;
    /* Count how many bytes are available for fetch(). */
    size_t avail() const;
    /* Returns how many bytes have ever been consumed from this buffer
     * using fetch(), send(), or discard(), minus those returned with
     * pushback(). */
    size_t offset() const;

    /* Fetch @amt bytes from the front of the buffer and discard them.
     * Equivalent to:
     *
     * b = malloc(amt);
     * fetch(b, amt);
     * free(b);
     *
     * But usually more efficient. */
    void discard(size_t amt);

    /* Return a pointer to a range of the buffer, defragmenting as
     * necessary to ensure that the region is linear.  @start and @end
     * are relative to the initial empty buffer i.e. linearise(x, y)
     * returns the x'th to y'th bytes to be added to the end of the
     * buffer.  This is the same coordinate system as used by
     * offset().  @start is inclusive and @end exclusive i.e. there
     * are @end - @start bytes in the returned memory region.  The
     * returned pointer remains valid until the buffer is destroyed,
     * or the relevant bytes of the buffer are consumed by fetch() or
     * discard(), or linearise() is called again. */
    const void *linearise(size_t start, size_t end);

    /* Fetch a byte from the buffer.  @i is a count of the number of
     * bytes extracted from the buffer before the returned one i.e. it
     * is in the same coordinate system as offset() */
    char idx(size_t i) const;

    static void test(class test &);
};

#endif /* !BUFFER_H__ */
