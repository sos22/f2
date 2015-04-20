#ifndef BUFFER_H__
#define BUFFER_H__

#include <stddef.h>

#include "error.H"
#include "fields.H"
#include "list.H"
#include "maybe.H"
#include "steal.H"
#include "timestamp.H"

class clientio;
class deserialise1;
class fd_t;
template <typename> class orerror;
class serialise1;
class subscriber;
class subscriptionbase;
class test;

class buffer;
class bufferfield;

namespace fields {
const bufferfield &mk(const buffer &);
}

class buffer {
    friend class bufferfield;
    class subbuf {
    public:
        subbuf *next;
        /* Buffer offset of payload[0] (which might not be interesting
         * data if we have start slack). */
        size_t startoff;
        /* Buffer offset of last allocated byte in payload (which
         * might be slack, or might even be past the end of the
         * buffer) */
        size_t endoff;
        /* Number of unused bytes at the beginning of the payload
         * space. */
        unsigned startslack;
        /* Number of unused bytes at the end of the payload
         * space. */
        unsigned endslack;
        void squashstartslack();
        static subbuf *fresh(size_t start, size_t minsize);
        /* Convert an offset in buffer space to a pointer into the
         * payload area. */
        void *payload(size_t offset) {
            return (void *)((uintptr_t)_payload - startoff + offset); }
        /* Buffer offset of the first byte in this subbuffer. */
        size_t start() const { return startoff + startslack; }
        /* Buffer offset of the last byte in this subbuffer, plus
         * one. */
        size_t end() const { return endoff - endslack; }
        /* Number of bytes in this subbuffer. */
        unsigned size() const { return (unsigned)(end() - start()); }
        unsigned char _payload[]; };
    /* There is always at least one sub-buffer in the buffer, even
     * when the buffer is empty. */
    subbuf *first;
    subbuf *last;
    /* The last subbuffer to be accessed, to try to speed up linear
     * scans through large buffers a bit. */
    subbuf *mru;
    void operator=(const buffer &o) = delete;
public: buffer(const buffer &o);
public: buffer(_Steal, buffer &o);
public: buffer();
public: ~buffer();

public:  explicit buffer(deserialise1 &);
public:  void serialise(serialise1 &) const;

    /* Interface to fd_t */
    /* Send some bytes from the start of the buffer.  Only ever makes
     * a single write syscall, so won't block if poll returns POLLOUT.
     * Might leave some bytes in the buffer, even when it returns
     * success.  Returns NULL on success, error::timeout on timeout, a
     * notified subscription if sub gets notified while we're waiting,
     * or some other error if something goes wrong. */
    orerror<subscriptionbase *> send(clientio,
                                     fd_t,
                                     subscriber &sub,
                                     maybe<timestamp> deadline = Nothing);
    /* Send with blocking.  The FD must be in non-blocking mode. */
    orerror<void> sendfast(fd_t);

    /* Grab some bytes from the fd and put them at the end of the
     * buffer.  Only ever makes a single read syscall, so won't block
     * if poll returns POLLIN.  Optional limit on the number of bytes
     * to read. */
    orerror<void> receive(clientio,
                          fd_t,
                          maybe<timestamp> deadline = Nothing,
                          maybe<uint64_t> maxbytes = Nothing);
    /* Grab some bytes from the fd, waiting at most until the deadline
     * or until the subscriber becomes notified.  If the subscriber
     * becomes notified, clear and return the subscription.  If an
     * error occurs, return that error.  Otherwise, return NULL. */
    orerror<subscriptionbase *> receive(clientio,
                                        fd_t,
                                        subscriber &sub,
                                        maybe<timestamp> deadline = Nothing);
    /* Receive without blocking.  The FD must be in non-blocking mode. */
    orerror<void> receivefast(fd_t);

    /* Interface for setting up buffers from memory. */
    /* Copy some bytes from memory to the end of the buffer.  The
     * buffer is expanded as needed to make space. */
    void queue(const void *, size_t sz);
    /* Copy some bytes out of the front of the buffer, removing them
     * from the buffer as we do so.  It is an error to request more
     * bytes than are available.  Discards the bytes rather than
     * copying them if @buf is NULL. */
    void fetch(void *buf, size_t sz);
    /* Transfer the contents of @buf to the end of this buffer.  @buf
     * is made empty.  Equivalent to:
     *
     * size_t s = buf.avail();
     * void *p = malloc(s);
     * buf.fetch(p, s);
     * queue(p, s);
     * free(p);
     *
     * But usually quite a bit more efficient. */
    void transfer(buffer &buf);

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
    const void *linearise(size_t start, size_t end) const;
    void *linearise(size_t start, size_t end);

    template <typename t> t *linearise(size_t start) {
        return (t *)linearise(start, start + sizeof(t)); }
    template <typename t> const t *linearise(size_t start) const {
        return (const t *)linearise(start, start + sizeof(t)); }

    /* Fetch a byte from the buffer.  @i is a count of the number of
     * bytes extracted from the buffer before the returned one i.e. it
     * is in the same coordinate system as offset() */
    unsigned char idx(size_t i) const;

    /* Byte-by-byte comparison of two buffers.  Only considers the
     * content, and not the producer or consumer values. */
public: bool contenteq(const buffer &) const; };

class bufferfield : public fields::field {
private: bool showshape_;
public:  enum contentform {
        c_ascii,
        c_bytes,
        c_words,
        } content;
private: bool hiderepeats_;
private: const buffer &b;
public:  static const bufferfield &mk(bool,
                                      enum contentform,
                                      bool,
                                      const buffer &);
private: bufferfield(bool _showshape,
                     enum contentform _content,
                     bool _hiderepeats,
                     const buffer &_b);
private: bufferfield(const bufferfield &);
public:  const bufferfield &showshape() const;
public:  const bufferfield &bytes() const;
public:  const bufferfield &words() const;
public:  const bufferfield &showrepeats() const;
private: void fmt(fields::fieldbuf &) const;
};

#endif /* !BUFFER_H__ */
