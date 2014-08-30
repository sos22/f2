#ifndef RPCCONN_H__
#define RPCCONN_H__

#include "buffer.H"
#include "either.H"
#include "list.H"
#include "mastersecret.H"
#include "nonce.H"
#include "orerror.H"
#include "peername.H"
#include "pubsub.H"
#include "mutex.H"
#include "registrationsecret.H"
#include "slavename.H"
#include "socket.H"
#include "thread.H"
#include "timedelta.H"
#include "util.H"
#include "waitbox.H"
#include "walltime.H"
#include "wireproto.H"

class beaconresult;
class clientio;
class messageresult;
class slavename;

class rpcconnconfig {
    /* Maximum size of the per-connection outgoing queue before we
       start to apply back pressure. */
public: const unsigned maxoutgoingbytes;
    /* Maximum connection idle time before we send a PING */
public: const timedelta pinginterval;
    /* Once we send a ping, how long does the peer have to respond
       before we declare them to be dead? */
public: const timedelta pingdeadline;
    /* Default configuration */
public: static const rpcconnconfig dflt;
public: rpcconnconfig(unsigned _maxoutgoingbytes,
                      timedelta _pinginterval,
                      timedelta _pingdeadline);
WIREPROTO_TYPE(rpcconnconfig);
};

namespace fields {
class field;
const field &mk(const rpcconnconfig &);
}

class rpcconnauth {
    friend class rpcconn;

    /* Authentication state of an RPC connection.  Options:
     *
     * preinit -- haven't initialised rpcconnauth yet.  No state.
     * done -- auth protocol finished.  No further state.
     * waithello -- waiting for the other side to send HELLO.  State
     *              is mastersecret and registrationsecret which will
     *              be used to check the HELLO when it arrives.
     * sendhelloslavea -- send a HELLOSLAVE::A as soon as possible.
     *                    State is a registration secret.
     * waithelloslavea -- waiting for a HELLOSLAVE::A message.  State
     *                    is a registration secret and a waitbox to populate
     *                    once it arrives.
     * waithelloslaveb -- waiting for a HELLOSLAVE::B message.  State
     *                    is a registration secret and a nonce.
     * waithelloslavec -- waiting for a HELLOSLAVE::C message.  State is a
     *                    waitbox to populate when it arrives.
     *
     * Transitions:
     *
     * waithello -> done                  When a valid HELLO arrives.
     * sendhelloslavea -> waithelloslaveb When we send the HELLOSLAVE::A
     * waithelloslaveb -> done            When we send a HELLOSLAVE::C
     * waithelloslavea -> waithelloslavec When we send the HELLOSLAVE::B
     * waithelloslavec -> done            When we receive a HELLOSLAVE::C
     *
     * We can start in done, waithello, sendhelloslavea, or
     * waithelloslavea.
     */
private: enum states {
        s_preinit,
        s_done,
        s_waithello,
        s_sendhelloslavea,
        s_waithelloslavea,
        s_waithelloslaveb,
        s_waithelloslavec,
    };
private: enum states state;

public:  maybe<class slavename> slavename() const;

public:  static rpcconnauth mkdone();
public:  static rpcconnauth mkwaithello(
    const mastersecret &ms,
    const registrationsecret &rs);
public:  static rpcconnauth mksendhelloslavea(
    const registrationsecret &rs);
public:  static rpcconnauth mkwaithelloslavea(
    const registrationsecret &rs,
    waitbox<orerror<void> > *);

public:  ~rpcconnauth();
private: rpcconnauth(const rpcconnauth &o);
private: void operator=(const rpcconnauth &o) = delete;
private: rpcconnauth();

    /* Interface to rpcconn.  Returning Nothing indicates that the
     * message should go to normal processing.  Returning an error
     * (any error) indicates that the connection should be torn
     * down. */
private: maybe<messageresult> message(
    const wireproto::rx_message &rxm,
    const peername &peer);
    /* Send any initial message needed to get the machine going. */
private: void start(buffer &outbuffer);

    /* Per-state state */
private: struct done {
    maybe<class slavename> slave;
    done(const maybe<class slavename> &_slave);
    };
private: struct waithello {
    mastersecret ms;
    registrationsecret rs;
    waithello(const mastersecret &_ms, const registrationsecret &_rs);
    };
private: struct sendhelloslavea {
    registrationsecret rs;
    sendhelloslavea(const registrationsecret &_rs);
    };
private: struct waithelloslavea {
    registrationsecret rs;
    waitbox<orerror<void> > *wb;
    waithelloslavea(const registrationsecret &_rs,
                    waitbox<orerror<void> > *_wb);
    };
private: struct waithelloslaveb {
    registrationsecret rs;
    nonce n;
    waithelloslaveb(const registrationsecret &_rs, const nonce &_n);
    };
private: struct waithelloslavec {
    waitbox<orerror<void> > *wb;
    waithelloslavec(waitbox<orerror<void> > *_wb)
        : wb(_wb) {}
    };
#define MAX(a, b) ((a) > (b) ? (a) : (b))
private: unsigned char buf[
    MAX(MAX(sizeof(waithello), sizeof(sendhelloslavea)),
        MAX(MAX(sizeof(done), sizeof(waithelloslavea)),
            MAX(sizeof(waithelloslaveb), sizeof(waithelloslavec))))];
#undef MAX
};

class rpcconnstatus {
public: buffer::status_t outgoing;
public: fd_t::status_t fd;
public: wireproto::sequencer::status_t sequencer;
public: list<wireproto::rx_message::status_t> pendingrx;
public: peername::status_t peername_;
public: walltime lastcontact;
public: rpcconnconfig config;
public: rpcconnstatus(const buffer::status_t &_outgoing,
                      const fd_t::status_t &_fd,
                      const wireproto::sequencer::status_t &_sequencer,
                      const list<wireproto::rx_message::status_t> &_pendingrx,
                      const peername::status_t &_peername,
                      const walltime &_lastcontact,
                      const rpcconnconfig &_config)
    : outgoing(_outgoing),
      fd(_fd),
      sequencer(_sequencer),
      pendingrx(_pendingrx),
      peername_(_peername),
      lastcontact(_lastcontact),
      config(_config) {}
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

class rpcconn : protected thread {
    friend class thread;
    friend class pausedthread<rpcconn>;
    /* Each rpcconn has an associated thread which does IO for it. */
private: waitbox<bool> shutdown; /* To shut down the thread */
private: void run(clientio);

    /* The raw socket.  Accessible only to the connection thread. */
private: socket_t sock;

    /* Configuration parameters for the connection. */
private: const rpcconnconfig config;

    /* Outgoing buffers.  Any thread can add stuff to this queue but
     * only the connection thread drains it.  The amount of stuff in
     * the queue is bounded by config.max_outgoing_bytes, so as to
     * give sensible back-pressure on message producers.
     * @outgoingshrunk gets notified whenever more space becomes
     * available.  @outgoinggrew gets notified whenever more stuff
     * gets added to the queue. */
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
private: timestamp lastcontact_monotone;
private: walltime lastcontact_wall;

private: const peername peer_;

    /* Authentication state.  Ordering: auth lock is below coordinator
     * lock. */
private: mutable mutex_t authlock;
private: rpcconnauth _auth;
private: rpcconnauth &auth(mutex_t::token) { return _auth; }
private: const rpcconnauth &auth(mutex_t::token) const { return _auth; }

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
    const slavename &,
    args...);

    /* Connect to a remote slave server, using a given registration
     * secret. */
public: template<typename t, typename...args> static orerror<t *> connectslave(
    clientio,
    const peername &,
    const registrationsecret &,
    args...);

    /* Raw connect, without sending a HELLO. */
public: template<typename t, typename ... args> static orerror<t *> connect(
    clientio,
    const rpcconnauth &,
    const peername &,
    const rpcconnconfig &,
    args...);

    /* Establish a connection around an existing socket, without
     * sending any messages.  The connection can start receiving
     * messages as soon as its constructor returns. */
public: template<typename t, typename... args> static orerror<t *> fromsocket(
    socket_t,
    const rpcconnauth &auth,
    const rpcconnconfig &config,
    args &&...);

protected: class rpcconntoken {
        friend class rpcconn;
    private: thread::constoken thr;
    private: socket_t sock;
    public:  rpcconnauth auth;
    public:  rpcconnconfig config;
    public:  peername peer;
    private: rpcconntoken(const thread::constoken &,
                          socket_t,
                          const rpcconnauth &,
                          const rpcconnconfig &,
                          const peername &); };
protected: rpcconn(const rpcconntoken &token);

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

    /* Queue the reply to a message, waiting for outbuf space if
     * necessary. */
private: bool queuereply(clientio, wireproto::tx_message &msg);

    /* Allocate a sequence number suitable for constructing a new
     * req_message */
public: wireproto::sequencenr allocsequencenr();

    /* Get a peername for the thing on the other end of the
     * connection. */
public: peername peer() const;

    /* Get slavename of the remote host, if we know it. */
public: maybe<class slavename> slavename() const;

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
public:   orerror<void> send(
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
    private: callres(subscriptionbase *s)
        : content(either<subscriptionbase *,
                         const wireproto::rx_message *>::left(s)) {}
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
     * (although the other end probably won't receive the results).
     * Use the thread death protocol to finish tearing the connection
     * down. */
public:  void teardown();

    /* Re-expose the thread death protocol. */
public:  class deathtoken : public thread::deathtoken {
    public: deathtoken(thread::deathtoken t)
        : thread::deathtoken(t) {}
    };
public:  maybe<deathtoken> hasdied() const;
public:  class deathsubscription : public thread::deathsubscription {
    public: rpcconn *const conn;
    public: deathsubscription(subscriber &_sub, rpcconn *_owner)
        :  thread::deathsubscription(_sub, _owner),
           conn(_owner) {}
    };
public:  void join(deathtoken dt) { thread::join(dt); }
public:  void join(clientio io) { thread::join(io); }

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
     * drain(), teardown(), wait for hasdied(), join(). */
public:    void destroy(clientio);

    /* Status interface */
public: typedef rpcconnstatus status_t;
public: status_t status(
    maybe<mutex_t::token> coordinatorlock) const;
};

#endif
