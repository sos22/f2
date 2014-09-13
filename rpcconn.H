#ifndef RPCCONN_H__
#define RPCCONN_H__

#include "actortype.H"
#include "buffer.H"
#include "either.H"
#include "list.H"
#include "mastersecret.H"
#include "nonce.H"
#include "orerror.H"
#include "peername.H"
#include "pubsub.H"
#include "mutex.H"
#include "ratelimiter.H"
#include "registrationsecret.H"
#include "rpctest.H"
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
class slavename;
class rpcconn;

class rpcconnconfig {
    /* Maximum size of the per-connection outgoing queue before we
       start to apply back pressure.  Note that this is the limit at
       which we prevent further things being added to the queue; it is
       not a bound on the queue size.  i.e. if we're below the limit
       then we allow any sends to add to the queue immediately, even
       if they would take us far above the limit. */
public: unsigned maxoutgoingbytes;
    /* Maximum connection idle time before we send a PING */
public: timedelta pinginterval;
    /* Once we send a ping, how long does the peer have to respond
       before we declare them to be dead? */
public: timedelta pingdeadline;
    /* How often is the other side allowed to ping us before we start
     * slowing things down? */
public: ratelimiterconfig pinglimit;
    /* Default configuration */
public: static const rpcconnconfig dflt;
public: rpcconnconfig(unsigned _maxoutgoingbytes,
                      timedelta _pinginterval,
                      timedelta _pingdeadline,
                      const ratelimiterconfig &_pinglimiter);
public: rpcconnconfig(quickcheck);
public: bool operator==(const rpcconnconfig &o) const;
WIREPROTO_TYPE(rpcconnconfig);
};

namespace fields {
class field;
const field &mk(const rpcconnconfig &);
}

template <typename t> class parser;
namespace parsers {
const parser<rpcconnconfig> &_rpcconnconfig();
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
public:  maybe<class actortype> type() const;

public:  static rpcconnauth mkdone(
    const class slavename &theirname,
    actortype theirtype);
    /* Create an rpcconnauth in the waithello state.  finished will be
     * invoked when we receive a valid hello method (or with an error
     * if the connection is torn down before that happens).  Note that
     * finished() is invoked under the connection auth lock, so must
     * not call back into slavename() or type(). */
public:  static rpcconnauth mkwaithello(
    const mastersecret &ms,
    const registrationsecret &rs,
    const std::function<orerror<void> (orerror<rpcconn *>,
                                       mutex_t::token)> &finished);
public:  static rpcconnauth mksendhelloslavea(
    const registrationsecret &rs,
    const class slavename &ourname,
    actortype ourtype);
public:  static rpcconnauth mkwaithelloslavea(
    const registrationsecret &rs,
    waitbox<orerror<void> > *,
    const class slavename &,
    actortype ourtype);

public:  ~rpcconnauth();
private: rpcconnauth(const rpcconnauth &o);
private: void operator=(const rpcconnauth &o) = delete;
private: rpcconnauth();

    /* Interface to rpcconn.  Return Nothing to indicate that the
     * message should go to normal processing.  Return a message to
     * skip normal processing and send the returned message instead.
     * Returning an error (any error) indicates that the connection
     * should be torn down.  Return NULL to take no further actions at
     * all with the received message. */
    /* Note that messages generated by this interface bypass the
     * maxoutgoingbytes limit.  rpcconnauth is responsible for making
     * sure that the backlog remains sensible, even when the peer is
     * misbehaving. */
private: maybe<orerror<wireproto::tx_message *> > message(
    rpcconn *conn,
    const wireproto::rx_message &rxm,
    const peername &peer,
    mutex_t::token authlock);
    /* Send any initial message needed to get the machine going. */
private: void start(buffer &outbuffer);
    /* Do whatever processing is necessary when a connection goes
     * away. */
private: void disconnect(mutex_t::token authlock);

    /* Per-state state */
private: struct done {
    class slavename slave;
    actortype type;
    done(const class slavename &_slave,
         actortype _type);
    };
private: struct waithello {
    mastersecret ms;
    registrationsecret rs;
    const std::function<orerror<void> (orerror<rpcconn *>,
                                       mutex_t::token)> finished;
    waithello(
        const mastersecret &_ms,
        const registrationsecret &_rs,
        const std::function<orerror<void> (orerror<rpcconn *>,
                                           mutex_t::token)> &finished);
    /* Surprise! The default copy constructor won't copy functions.  I
       don't know why. */
    waithello(const waithello &o);
    };
private: struct sendhelloslavea {
    registrationsecret rs;
    class slavename ourname;
    actortype ourtype;
    sendhelloslavea(const registrationsecret &_rs,
                    const class slavename &_ourname,
                    actortype _ourtype);
    };
private: struct waithelloslavea {
    registrationsecret rs;
    waitbox<orerror<void> > *wb;
    class slavename ourname;
    actortype ourtype;
    waithelloslavea(const registrationsecret &_rs,
                    waitbox<orerror<void> > *_wb,
                    const class slavename &_ourname,
                    actortype _ourtype);
    };
private: struct waithelloslaveb {
    registrationsecret rs;
    nonce n;
    class slavename ourname;
    waithelloslaveb(const registrationsecret &_rs,
                    const nonce &_n,
                    const class slavename &_slavename);
    };
private: struct waithelloslavec {
    waitbox<orerror<void> > *wb;
    actortype remotetype;
    waithelloslavec(waitbox<orerror<void> > *_wb, actortype _remotetype)
        : wb(_wb),
          remotetype(_remotetype) {}
    };
#define MAX(a, b) ((a) > (b) ? (a) : (b))
private: unsigned char buf[
    MAX(MAX(sizeof(waithello), sizeof(sendhelloslavea)),
        MAX(MAX(sizeof(done), sizeof(waithelloslavea)),
            MAX(sizeof(waithelloslaveb), sizeof(waithelloslavec))))];
#undef MAX
};

class rpcconnstatus {
    friend class rpcconn;
public:  buffer::status_t outgoing;
public:  fd_t::status_t fd;
public:  wireproto::sequencer::status_t sequencer;
public:  peername::status_t peername_;
public:  walltime lastcontact;
public:  maybe<slavename> otherend;
public:  maybe<actortype> otherendtype;
public:  rpcconnconfig config;
public:  unsigned const pendingtxcall;
public:  unsigned const pendingrxcall;
private: rpcconnstatus(const buffer::status_t &_outgoing,
                       const fd_t::status_t &_fd,
                       const wireproto::sequencer::status_t &_sequencer,
                       const peername::status_t &_peername,
                       const walltime &_lastcontact,
                       const maybe<slavename> &_otherend,
                       maybe<actortype> _otherendtype,
                       const rpcconnconfig &_config,
                       unsigned _pendingtxcall,
                       unsigned _pendingrxcall)
    : outgoing(_outgoing),
      fd(_fd),
      sequencer(_sequencer),
      peername_(_peername),
      lastcontact(_lastcontact),
      otherend(_otherend),
      otherendtype(_otherendtype),
      config(_config),
      pendingtxcall(_pendingtxcall),
      pendingrxcall(_pendingrxcall) {}
public:  rpcconnstatus(quickcheck);
public:  bool operator==(const rpcconnstatus &o) const;
WIREPROTO_TYPE(rpcconnstatus);
};
namespace fields {
const field &mk(const rpcconnstatus &);
}

class rpcconn : protected thread {
    friend class postedcall;
    friend class thread;
    friend class pausedthread<rpcconn>;
    friend void tests::_rpc();
    /* Each rpcconn has an associated thread which does IO for it. */
private: waitbox<bool> shutdown; /* To shut down the thread */
private: void run(clientio);

    /* The raw socket.  Accessible only to the connection thread. */
private: socket_t sock;

    /* Configuration parameters for the connection. */
private: const rpcconnconfig config;

    /* Rate limiter for incoming pings, because we handle pings before
       authenticating and it'd be a bit sad if a ping flood could
       cause a DOS */
private: ratelimiter pinglimiter;

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

    /* When was the last time we had contact with the other side?
       Used to drive periodic pings and dead connection detection. */
private: mutex_t contactlock;
private: timestamp lastcontact_monotone;
private: walltime lastcontact_wall;

private: const peername peer_;

    /* Authentication state.  Ordering: auth lock is below coordinator
     * lock. */
    /* Mutable so that it can be acquired from the const status()
     * method. */
private: mutable mutex_t authlock;
private: rpcconnauth _auth;
private: rpcconnauth &auth(mutex_t::token);
private: const rpcconnauth &auth(mutex_t::token) const;

    /* Reference count machinery */
private: mutex_t referencelock;
private: cond_t referencecond;
    /* Simple refcount, protected by the referencelock.  referencecond
     * signalled when this reaches zero. */
private: int references;
    /* Set normally, cleared once endconn() returns.  For debug
     * purposes only.  Not synchronised; access with atomic ops only
     * if there is any possibility of a race. */
private: bool referenceable;

    /* State needed to process outgoing procedure calls. */
public:  class asynccall;
private: class _calls {
    public: class _send {
        public: list<asynccall *> pending;
            /* Cache of pending.length() */
        public: unsigned nrpending;
            /* Published whenever stuff gets added to the pending list. */
        public: publisher pub;
        public: _send();
        };
    public: class _recv {
        public: list<asynccall *> pending;
        public: _recv();
        };
        /* Lock ordering: beneath tx lock. */
        /* mutable because it's acquired from the status() method. */
    public: mutable mutex_t mux;
    public: _send send;
    public: _recv recv;
        /* Set once thread is too far through shutdown to accept more
         * calls. */
    public: bool finished;
    public: _calls();
    } calls;

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
    actortype ourtype,
    const rpcconnconfig &,
    args...);

    /* Connect to a remote slave server, using a given registration
     * secret. */
public: template<typename t, typename...args> static orerror<t *> connectslave(
    clientio,
    const peername &,
    const registrationsecret &,
    const slavename &ourname,
    actortype ourtype,
    const rpcconnconfig &,
    args...);

    /* Connect with no authentication at all.  Only really sensible
     * for UNIX domain sockets. */
public: template<typename t, typename ... args>static orerror<t *>connectnoauth(
    clientio,
    const class slavename &theirname,
    actortype theirtype,
    const peername &,
    const rpcconnconfig &,
    args &&...);

    /* Raw connect.  Caller specifies the type of authentication explicitly. */
public: template<typename t, typename ... args> static orerror<t *> connect(
    clientio,
    const rpcconnauth &,
    const peername &,
    const rpcconnconfig &,
    args &&...);

    /* Establish a connection around an existing socket, without
     * sending any messages.  The connection can start receiving
     * messages as soon as its constructor returns. */
public: template<typename t, typename... args>
    static orerror<t *> fromsocket(
    socket_t,
    const rpcconnauth &auth,
    const rpcconnconfig &config,
    args &&...);

    /* Wrapper around fromsocket() for connections which require no
     * further authentication. */
public: template<typename t, typename... args>
    static orerror<t *> fromsocketnoauth(
    socket_t,
    const class slavename &theirname,
    actortype theirtype,
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

    /* Bits to process incoming messages.  Servers should override
     * these methods; the default implementation doesn't do very
     * much. */
    /* Process a message received from the other end of the
     * connection.  The rpcconn is stopped while this is running and
     * will not send or receive anything, so message() methods should
     * generally not attempt any further client IO (and hence do not
     * receive a clientio token.  The message() method must return one
     * of three possible types of return value:
     *
     * -- an error, to return that error to the client;
     * -- a wireproto resp_message, to queue that message for
     *    transmission as a reply to the call; or
     * -- a posted call, indicating that the derived class will
     *    generate a reply asynchronously and eventually queue it up
     *    itself.
     *
     * For details of posted calls, see the postedcall class. */
    class postedcall;
public:    class messageresult {
        friend class rpcconn;
    private: enum _flavour {
            mr_success,
            mr_failure,
            mr_posted
        };
        /* A wireproto::resp_message for mr_success, an error for
         * mr_failure, or NULL for mr_posted. */
        /* (We can't store a pointer to the postedcall because it
         * remains owned by the message() implementation, which might
         * asynchronously complete() or fail() it at any time) */
    private: orerror<wireproto::resp_message *> const content;
    private: enum _flavour flavour() const;
    private: wireproto::resp_message *success() const;
    private: error failure() const;
    public:  messageresult(error e)
        : content(e) {}
    public:  messageresult(wireproto::resp_message *m)
        : content(m) {}
    public:  messageresult(postedcall *)
        : content(NULL) {}
    };
public:    class messagetoken {
        friend class rpcconn;
    private: messagetoken() {};
    };
protected: virtual messageresult message(
        const wireproto::rx_message &,
        messagetoken);

    /* A representation of a call which is too complex to be completed
     * directly in the message() call.  message() implementations can
     * allocate one of these and return it as their messageresult,
     * causing the rpcconn to process any other messages which might
     * be waiting to be sent or received, and then complete the posted
     * call once it is convenient to do so. */
    /* Note that postedcalls has no abort() or abandon() method: once
     * a postedcall is started it must always reply to the client,
     * whether successfully (with complete()) or unsuccessfully (with
     * fail()). */
protected: class postedcall {
        friend class rpcconn;
        /* Partially-constructed reply to the call which initiated
         * us. */
    private: wireproto::resp_message resp;
        /* rpcconn on which the reply will ultimately be sent.  Note
         * that this can become NULL if the connection is lost before
         * the postedcall is completed. */
    private: rpcconn *owner;
        /* Protects @owner and @completed. */
    private: mutex_t mux;
        /* Set once the complete() or fail() methods have been called.
         * If the rpcconn thread observes this to be set while it
         * holds @mux then it will delete the posted call. */
    private: bool completed;
        /* Construct a new postedcall in response to a received
         * message.  This is only safe to call from the message()
         * method (hence the messagetoken parameter).  The new
         * structure must be returned by message(). */
    public:  postedcall(
            rpcconn *,
            const wireproto::rx_message &,
            messagetoken);
        /* Add an additional parameter to the response to the call.
         * This is essentially the same as a wireproto message's
         * method of the same name. */
    public:  template <typename t> postedcall &addparam(
            wireproto::parameter<t>, const t &);
        /* Mark the call as failed.  Any parameters which have been
         * added are removed and the peer is sent an error reply.
         * This acts as a destructor: once fail() is called, the
         * postedcall is only accessible to the rpcconn
         * implementation; whoever called fail() should treat it as
         * having been released. */
        /* Note that this method will wait for outgoing buffer space,
         * and so requires a clientio token. */
    public:  void fail(clientio, error);
        /* Mark the reply as finished and start sending it to the
         * other end of the connection.  Like fail(), this acts as a
         * destructor on the message and so the caller should not
         * access it again.  Also like fail(), it waits for space in
         * the outgoing queue, so requires a clientio token. */
    public:  void complete(clientio);
        /* Use complete() or fail() instead. */
    private: ~postedcall(); };
    /* rpcconn maintains a list of all of the postedcalls pending
     * against itself, so that it can cancel them all when it shuts
     * down (and for debugging).  Only accessed from the conn
     * thread, so no synchronisation. */
private: list<postedcall *> postedcalls;

    /* The connection is going away and has finished processing its
     * last message.  It's still safe to call send() and call() but,
     * depending on the reason for the shutdown, that might just give
     * back an error.  It is not safe to create further references to
     * the rpcconn with reference() once endconn() returns; the
     * endconn() implementation must ensure that that does not
     * happen. */
private: virtual void endconn(clientio) {}

    /* Connection reference counting.  Each rpcconn has an internal
     * reference count and hasdied() will not return a valid death
     * token until that reference count reaches zero (the connection
     * to the remote peer might, of course, be lost at any time, so
     * holding a reference does not imply that call()s will succeed;
     * it just makes sure that they won't reference freed memory if
     * they happen to race with a destroy() call).  reference()
     * increments the internal refcount and returns a token for the
     * new reference.  unreference() releases this token.  Note that
     * the death subscription will not fire and that join(clientio)
     * and destroy(clientio) will block until the last reference is
     * released, so reference tokens behave quite a lot like mutex
     * tokens when considering deadlocks.  The tokens themselves have
     * handle-like semantics i.e. the structure itself can be copied
     * at will but all copies will refer to the same ``logical'' token
     * and, in particular, releasing one of the copies will release
     * all of them. */
    /* Note that reference() is unsafe once endconn() has returned.
     * endconn() must ensure that no further references are created to
     * the conn. */
public:  class reftoken {
        friend class rpcconn;
    private: reftoken() {}
    };
public:  reftoken reference();
public:  void unreference(reftoken);

    /* Queue the reply to a message, waiting for outbuf space if
     * necessary. */
private: bool queuereply(clientio, wireproto::tx_message &msg);

    /* Try to transfer messages from the outgoing call queue to the
     * main send buffer until we hit the maxoutgoingbytes limit.  Must
     * be called under the TX lock, and will acquire and release the
     * call lock. */
private: void sendcalls(mutex_t::token /* txlock */);

    /* We just received a reply.  Find the matching asynccall and
     * complete it.  Acquires and releases the calls lock. */
private: void receivereply(wireproto::rx_message *);

    /* Allocate a sequence number suitable for constructing a new
     * req_message */
public: wireproto::sequencenr allocsequencenr();

    /* Get a peername for the thing on the other end of the
     * connection. */
public: peername peer() const;

    /* Get slavename of the remote host, if we know it. */
public: maybe<class slavename> slavename() const;
    /* Get actortype of the remote host, if we know it. */
public: maybe<actortype> type() const;
    /* Get the peername for the local end of the connection */
public: peername localname() const;

    /* Bits to send outgoing messages. */
    /* Queue a message to be sent.  This can block if the outgoing
     * queue is full, so needs a clientio token.  Caller can specify a
     * deadline, causing the send to fail after a set time if it can't
     * get space.  Messages are always either queued or not queued;
     * they never get half-added to the queue. */
    /* Note that this adds stuff to the send queue; it doesn't
     * necessarily send it, and it certainly doesn't wait for the
     * other side to receive it.  e.g. it might still be in the
     * outgoing buffer, or the kernel TX buffer, or on the wire, or
     * ... */
    /* XXX unilateral sends are currently only used by the test
     * harness; should maybe come up with a simpler scheme. */
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

    /* Asynchronous calls: start a call but return before it
     * completes. */
public: class asynccall {
        friend class rpcconn;
    private: wireproto::sequencenr const sequence;
    private: rpcconn *const owner;
        /* Nothing until we get a reply, NULL once the reply's been
         * fetched by the client. */
    private: maybe<orerror<const wireproto::rx_message *> > _result;
        /* Protects _result field.  Ordering: after the main calls
         * mux, leaf lock. */
    private: mutex_t mux;
    private: buffer sendbuf;
        /* Set when result() goes from Nothing to just(). */
    public:  publisher pub;
    private: asynccall(wireproto::sequencenr, rpcconn *);
        /* Extract the result of the call.  Returns Nothing if the
         * call is still outstanding, an error if there were some
         * error, or the result message otherwise.  This will only
         * return success once; calling it again once you have a
         * successful return is an error. */
    public:  maybe<orerror<const wireproto::rx_message *> > popresult();
        /* Not a real destructor because it needs to acquire locks.
         * Safe to call without first calling popresult(), in which
         * case it will not be possible to check the result of the
         * call.  If destroy() is called early enough then the rpcconn
         * will try to abort making the call, but never guarantees to
         * do so, and gives no indication of whether it did. */
    public:  void destroy();
        /* Use popresult() or destroy() instead. */
    private: ~asynccall();
    };
    /* Note no clientio token: this never waits!  In particular, it
     * doesn't wait for TX space, so will not apply any kind of
     * backpressure.  Callers are responsible for keeping the number
     * of async calls down to something sensible. */
public: asynccall *callasync(const wireproto::req_message &msg);

    /* Start a teardown sequence on a connection.  Outgoing sends are
     * cancelled and no further incoming messages will be accepted.
     * Messages which are still being processed will be completed
     * (although the other end probably won't receive the results).
     * Use the thread death protocol to finish tearing the connection
     * down. */
public:  void teardown();

    /* Set once the rpcconn thread has gone far enough through its
     * shutdown sequence that it will no longer send or receive
     * messages.  This happens shortly before the thread itself
     * exits. */
private: waitbox<void> _threadfinished;

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
public: status_t status() const;
};

namespace tests {
template <typename> class event;
namespace __rpcconn {
extern event<wireproto::tx_message **> sendinghelloslavec;
extern event<rpcconn *> threadawoken;
extern event<void> replystopped;
extern event<rpcconn *> calldestroyrace1;
extern event<rpcconn *> calldestroyrace2;
extern event<rpcconn *> calldestroyrace3;
}
}

#endif
