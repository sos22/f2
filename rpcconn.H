#ifndef RPCCONN_H__
#define RPCCONN_H__

#include "buffer.H"
#include "either.H"
#include "list.H"
#include "mastersecret.H"
#include "orerror.H"
#include "peername.H"
#include "pubsub.H"
#include "mutex.H"
#include "registrationsecret.H"
#include "socket.H"
#include "thread.H"
#include "waitbox.H"
#include "wireproto.H"

class beaconresult;
class clientio;

class rpcconnauth {
public:  class hello_ {
    friend class rpcconnauth;
    private: hello_(
        const mastersecret &_ms,
        const registrationsecret &_rs)
        : ms(_ms),
          rs(_rs) {}
    public:  mastersecret ms;
    public:  registrationsecret rs;
    };
private: maybe<hello_> content;
public:  static rpcconnauth needhello(
    const mastersecret &ms,
    const registrationsecret &rs) { return rpcconnauth(ms, rs); }
private: rpcconnauth(
    const mastersecret &ms,
    const registrationsecret &rs)
        : content(hello_(ms, rs)) {}
public:  static rpcconnauth authenticated() { return rpcconnauth(); }
private: explicit rpcconnauth()
    : content(Nothing) {}
public:  bool needhello() const { return content.isjust(); }
public:  const hello_ &hello() const { return content.just(); }
};

class rpcconnstatus {
public: buffer::status_t outgoing;
public: fd_t::status_t fd;
public: wireproto::sequencer::status_t sequencer;
public: list<wireproto::rx_message::status_t> pendingrx;
public: peername::status_t peername_;
public: timestamp lastcontact;
public: rpcconnstatus(const buffer::status_t &_outgoing,
                      const fd_t::status_t &_fd,
                      const wireproto::sequencer::status_t &_sequencer,
                      const list<wireproto::rx_message::status_t> &_pendingrx,
                      const peername::status_t &_peername,
                      const timestamp &_lastcontact)
    : outgoing(_outgoing),
      fd(_fd),
      sequencer(_sequencer),
      pendingrx(_pendingrx),
      peername_(_peername),
      lastcontact(_lastcontact) {}
public: ~rpcconnstatus() { pendingrx.flush(); }
WIREPROTO_TYPE(rpcconnstatus);
};
namespace fields {
const field &mk(const rpcconnstatus &);
}

class messageresult {
private: orerror<maybe<wireproto::tx_message *> > content;
private: messageresult() : content(Nothing) {}
public:  messageresult(error e) : content(e) {}
public:  messageresult(wireproto::tx_message *m)
    : content(m) { assert(m); }
public:  static const messageresult noreply;
public:  bool isfailure() const { return content.isfailure(); }
public:  error failure() const { return content.failure(); }
public:  bool isreply() const {
    return content.issuccess() && content.success().isjust(); }
public:  wireproto::tx_message *reply() const {
    assert(content.issuccess());
    assert(content.success().isjust());
    return content.success().just(); } };

class rpcconn : threadfn {
    /* Each rpcconn has an associated thread which does IO for it. */
private: thread *thr;
private: waitbox<bool> shutdown; /* To shut down the thread */
private: void run(clientio);

    /* The raw socket.  Accessible only to the connection thread. */
private: socket_t sock;

    /* Outgoing buffers.  Any thread can add stuff to this queue but
     * only the connection thread drains it.  The amount of stuff in
     * the queue is bounded, so as to give sensible back-pressure on
     * message producers.  @outgoingshrunk gets notified whenever more
     * space becomes available.  @outgoinggrew gets notified whenever
     * more stuff gets added to the queue. */
private: static const unsigned MAX_OUTGOING_BYTES = 16384;
private: mutex_t txlock;
private: buffer outgoing;
private: publisher outgoingshrunk;
private: publisher outgoinggrew;

    /* Sequence number allocator.  Accessible to any thread. */
private: mutex_t sequencelock;
private: wireproto::sequencer sequencer;

    /* Incoming buffers.  Only the connection thread can add stuff to
     * this queue, any thread can take things out. */
    /* lock is mutable so that we can acquire it from the status()
       method, to get status of pendingrx */
private: mutable mutex_t rxlock;
private: list<const wireproto::rx_message *> pendingrx;
private: publisher pendingrxextended;

    /* When was the last time we had contact with the other side?
       Used to drive periodic pings and dead connection detection. */
private: mutex_t contactlock;
private: timestamp lastcontact;

private: const peername peer_;

    /* Authentication state.  Only accessed from the connection
     * thread. */
private: rpcconnauth auth;

private: rpcconn() = delete;
private: rpcconn(const rpcconn &) = delete;
private: void operator=(const rpcconn &) = delete;

    /* Establising a connection. */
    /* Connect to a remote master server, using the results of the
     * beacon protocol.  Done as a template so that you can specify a
     * derived class with a more complete startconn(), message(), and
     * endconn() suite. */
public: template<typename t, typename...args> static orerror<t *> connectmaster(
    clientio,
    const beaconresult &,
    args...);

    /* Raw connect, without sending a HELLO. */
public: template<typename t, typename ... args> static orerror<t *> connect(
    clientio,
    const peername &,
    args...);

    /* Establish a connection around an existing socket, without
     * sending any messages.  The connection can start receiving
     * messages as soon as its constructor returns. */
public: template<typename t, typename... args> static orerror<t *> fromsocket(
    socket_t,
    const rpcconnauth &auth,
    args...);

protected: rpcconn(socket_t _sock,
                   const rpcconnauth &_auth,
                   const peername &_peer);

private: messageresult hellomessage(const wireproto::rx_message &);

    /* Bits to process incoming messages.  Servers should override
     * these methods; the default implementation doesn't do very
     * much. */
    /* Process a message received from the other end of the
     * connection.  Should return either an error, to send an error
     * reply, or a response message, or the special value
     * messageresult::noreply.  This should generally not wait for
     * client IO, to avoid deadlocks. */
protected: virtual messageresult message(const wireproto::rx_message &);
    /* The connection is going away and has finished processing its
     * last message.  It's still safe to call send() and call() but,
     * depending on the reason for the shutdown, that might just give
     * back an error. */
private: virtual void endconn(clientio) {}

    /* Allocate a sequence number suitable for constructing a new
     * req_message */
public: wireproto::sequencenr allocsequencenr();

    /* Release a sequence number previously returned by
     * ::allocsequencenr() */
public: void putsequencenr(wireproto::sequencenr);

    /* Get a peername for the thing on the other end of the
     * connection. */
public: peername peer() const;

    /* Bits to send outgoing messages. */
    /* Queue a message to be sent.  This can block if the outgoing
     * queue is full, so needs a clientio token.  Caller can specify a
     * deadline, causing the send to fail after a set time if it can't
     * get space, and a subscriber block, causing the send to fail if
     * some other thing gets notified.  Messages are always either
     * queued or not queued; they never get half-added to the
     * queue. */
    /* Note that this adds stuff to the send queue; it doesn't
     * necessarily send it, and it certainly doesn't wait for the
     * other side to receive it.  e.g. it might still be in the
     * outgoing buffer, or the kernel TX buffer, or on the wire, or
     * ... */
public:  class sendres {
        friend class rpcconn;
    private: orerror<maybe<subscriptionbase *> > const content;
    private: sendres(error e) : content(e) {}
    private: sendres(subscriptionbase *s) : content(s) {}
    private: sendres() : content(Nothing) {}
    public:  bool isfailure() const { return content.isfailure(); }
    public:  error failure() const { return content.failure(); }
    public:  bool isnotified() const {
        return content.issuccess() && content.success().isjust(); }
    public:  subscriptionbase *notified() const {
        return content.success().just(); }
    public:  bool issuccess() const {
        return content.issuccess() && content.success().isnothing(); }
};
public:  sendres send(
    clientio,
    const wireproto::tx_message &msg,
    subscriber &sub,
    maybe<timestamp> deadline = Nothing);
public:   maybe<error> send(
    clientio,
    const wireproto::tx_message &msg,
    maybe<timestamp> deadline = Nothing);

    /* RPC interface.  Perform a remote procedure call and get back
     * the result. */
public:  class callres {
        friend class rpcconn;
    private: orerror<either<subscriptionbase *,
                            const wireproto::rx_message *> > const
        content;
    private: callres(error e) : content(e) {}
    private: callres(subscriptionbase *s) : content(s) {}
    private: callres(const wireproto::rx_message *m) : content(m) {}
    public:  bool isfailure() const { return content.isfailure(); }
    public:  error failure() const { return content.failure(); }
    public:  bool isnotified() const {
        return content.issuccess() &&
            content.success().isleft(); }
    public:  subscriptionbase *notified() const {
        assert(isnotified());
        return content.success().left(); }
    public:  bool issuccess() const {
        return content.issuccess() &&
            content.success().isright(); }
    public:  const wireproto::rx_message *success() const {
        assert(issuccess());
        return content.success().right(); } };
public:  callres call(
    clientio,
    const wireproto::req_message &msg,
    subscriber &sub,
    maybe<timestamp> deadline = Nothing);
public: orerror<const wireproto::rx_message *> call(
    clientio,
    const wireproto::req_message &msg,
    maybe<timestamp> deadline = Nothing);

    /* Start a teardown sequence on a connection.  Outgoing sends are
     * cancelled and no further incoming messages will be accepted.
     * Messages which are still being processed will be completed
     * (although the other end probably won't receive the results). */
public:  void teardown();

    /* A deathtoken is a kind of proof that the connection has died.
     * Holding one gives you permission to call destroy().  They can
     * only be obtained by calling hasdied() and getting back a valid
     * result. */
    class deathtoken {
        friend class rpcconn;
    private: deathtoken() {};
    };
public:    maybe<deathtoken> hasdied() const;

    /* A publisher which gets notified whenever hasdied() goes from
       Nothing to a valid token. */
public:    const publisher &deathpublisher() const;

    /* Finish tearing down a connection which has died.  This is the
     * only way of destroying an RPC conn. */
public:    void destroy(deathtoken);

    /* Should only be called from the destroy() method. */
protected: virtual ~rpcconn();

    /* Wait for the outgoing queue to drain.  There's no guarantee
     * that the other side has received the data, just that it won't
     * be discarded by destroy(). */
    /* Note that this waits for the queue to become empty, *not* for
     * already-queued messages to be sent.  That means that it's only
     * guaranteed to finish if there's nobody adding more stuff to the
     * queue. */
public:    void drain(clientio);

    /* Convenience function which runs the entire shutdown protocol:
     * drain(), teardown(), wait for a hasdied(), destroy(). */
public:    void destroy(clientio);

    /* Status interface */
public: typedef rpcconnstatus status_t;
public: status_t status(
    maybe<mutex_t::token> coordinatorlock) const;
};

#endif
