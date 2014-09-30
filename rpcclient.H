#ifndef RPCCLIENT_H__
#define RPCCLIENT_H__

#include "buffer.H"
#include "either.H"
#include "maybe.H"
#include "mutex.H"
#include "orerror.H"
#include "pubsub.H"
#include "tuple.H"
#include "version.H"
#include "waitbox.H"

/* Most RPC client configuration parameters are only really useful
 * from test programs. */
#define _rpcclientconfig(ctxt, iter0, iter1)                          \
    /* Version of protocol to use when connecting. */                 \
    iter0(ctxt, 1, mand, value, ::version, vers)                      \
    /* Size of socket receive buffer.  Only really useful to */       \
    /* change this in test programs. */                               \
    iter1(ctxt, 1, opt, value, unsigned, socketrcvsize)

class rpcclientconfig {
    mktuplefields(rpcclientconfig, public);
public: static rpcclientconfig dflt(); };
mktupleext(rpcclientconfig)

namespace wireproto {
class req_message;
class resp_message;
class sequencenr; }

class clientio;
class peername;

class rpcclientstatus {
};

class rpcclient {
public:  class asynccall;
public:  class asyncconnect;
private: class workerthread;

private: workerthread *const worker;

    /* Synchronous connect.  Given a remote peername, connect to it,
     * with an optional timeout, establish an rpcclient, and return
     * it.  Potentially blocks forever if no deadline is specified.
     * Returns error::timeout if the deadline expires before the
     * connect() completes. */
public: static orerror<rpcclient *> connect(
    clientio,
    const peername &,
    maybe<timestamp> deadline = Nothing,
    const rpcclientconfig & = rpcclientconfig::dflt());

    /* Asynchronous connect.  Start connecting to a remote host
     * without waiting for the connect to complete.  Returns an
     * asyncconnect structure which can be used to wait for the
     * connect to complete and to collect its results once it does. */
    /* Note that a connect() is only considered complete once the
     * initial HELLO call returns; this may be some time after the
     * connect() syscall succeeds. */
    /* Note that the asyncconnect structure is embedded in the
     * workerthread one, so can only be deleted by deleting the
     * rpcconn. */
public: class asyncconnect {
    friend class rpcclient;
        /* Protects all of our internal fields.  mutable because it's
         * acquired from finished(), which is const. */
    private: mutable mutex_t mux;
        /* Published whenever res changes. */
    private: publisher _pub;
        /* -- Nothing while the connect is still running.
         * -- Success if the connect completed successfully.
         * -- An appropriate error if the connect failed. */
    private: maybe<orerror<void> > res;
        /* The rpcconn structure which we're trying to set up. */
    private: rpcclient *client() const;
        /* Use rpcclient::connect() */
    private: asyncconnect();
        /* Return Nothing if the call has not yet completed or a
         * token if it has.  The token can then be used to call pop()
         * and get the result of the connect. */
    public: class token {
        friend class asyncconnect;
            /* Use finished() instead. */
        private: token(); };
    public:  maybe<token> finished() const;
        /* Published if finished() changes from Nothing to something. */
    public:  const publisher &pub() const;
        /* Extract the result of the connect, which is either an error
         * or a fresh rpcconn structure.  Only valid once finished()
         * returns non-Nothing.  Destroys the asyncconnect
         * structure. */
    public:  orerror<rpcclient *> pop(token);
        /* Abort the attempt to connect to the remote peer, without
         * waiting for it to complete.  Releases the asyncconnect
         * structure.  If the connect had already completed when this
         * is called then the new connection is immediately torn
         * down. */
    public:  void abort();
        /* Use pop() or abort() */
    private: ~asyncconnect(); };
public: static asyncconnect *connect(
        const peername &,
        const rpcclientconfig & = rpcclientconfig::dflt());

    /* Use connect() */
private: explicit rpcclient(workerthread *);

    /* Synchronous RPC calls against the remote peer.  The call()
     * method takes the request message to send and, optionally, a
     * timeout.  It returns either a pointer to the message response
     * (which the caller should later delete) or an error.  In the
     * case of timeout, it returns error::timeout.
     *
     * Note that there is no way to distinguish between errors
     * generated in the RPC layers and error responses returned by the
     * remote host, and when an error occurs there is no way to
     * determine whether the remote host received the message.
     * The higher-level protocol must be designed such that these
     * ambiguities do not matter.
     */
public: orerror<wireproto::rx_message *> call(
    clientio,
    const wireproto::req_message &,
    maybe<timestamp> deadline = Nothing);

    /* Asynchronous RPC API.  The caller provides a request message
     * and is given back an asynccall structure which can be used to
     * wait for the remote peer to generate a response to the call and
     * to obtain the result once it does so.  Note that there is no
     * attempt to limit the number of calls outstanding on a given
     * connection, and so a caller which makes asynchronous call()s
     * faster than the remote peer can complete will generate a
     * potentially infinite backlog.  The caller is expected to apply
     * whatever flow control or backpressure is appropriate to prevent
     * the queue from growing without bounds.  The async call() call
     * guaranteed to complete quickly, even when the remote peer is
     * slow or disconnected, and so can safely be called whilst
     * holding an arbitrary combination of locks. */
    class asynccall {
        friend class rpcclient::workerthread;

        /* Protects res */
    private: mutable mutex_t mux;
        /* Nothing if the call is still in progress, left() if it was
         * aborted, right(error) if it failed, or right(msg) if it
         * succeeded. */
        /* XXX consider using a waitbox for this. */
    private: maybe<either<void, orerror<wireproto::rx_message *> > > res;
        /* Notified whenever res goes from Nothing to right */
    private: publisher _pub;
        /* The message to send to the peer (if we haven't sent it
         * yet).  Initialised by call during setup and then only
         * touched by the connection thread, so not synchronised. */
    private: buffer outbuf;
        /* Sequence number expected in replies. */
    private: wireproto::sequencenr const sequence;
        /* Use rpcconn::call() */
    private: asynccall(const wireproto::req_message &m,
                       wireproto::sequencenr sequence);
        /* Check whether the call has completed.  If it has, return a
         * completion token.  Otherwise, return Nothing. */
    public: class token {
        friend class asynccall;
            /* Use finished() instead. */
        private: token(); };
    public: maybe<token> finished() const;
        /* The publisher is guaranteed to be notified shortly after
         * finished() changes from Nothing to something. */
    public:  const publisher &pub() const;
        /* Extract the result of the call, or an error if the call
         * failed.  There is no way to reliably distinguish an error
         * generated by the remote peer from one generated by the RPC
         * layer.  On error, there is no way to tell whether the
         * remote peer received the message.  pop() can only be called
         * once finished() returns non-Nothing.  pop() destroys the
         * asynccall structure; the caller must not reference it again
         * after calling pop. */
    public:  orerror<wireproto::rx_message *> pop(token);
        /* Abandon an asynchronous call, releasing the structure
         * immediately.  There is no way for the caller to determine
         * if the remote peer has received the message.  This destroys
         * the asynccall structure. */
    public:  void abort();
        /* Use abort() or pop() */
    private: ~asynccall(); };
public:  asynccall *call(const wireproto::req_message &);

    /* Status interface */
public:  typedef rpcclientstatus status_t;
public:  status_t status() const;

    /* Tear down and release the connection, failing any outstanding
     * calls.  Note that this has no clientio token and so is
     * guaranteed to complete quickly, and is therefore safe to call
     * whilst holding locks. */
public:  ~rpcclient(); };

#endif /* !RPCCLIENT_H__ */
