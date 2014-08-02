#ifndef RPCSERVER_H__
#define RPCSERVER_H__

#include "list.H"
#include "listenfd.H"
#include "mutex.H"
#include "peername.H"
#include "pubsub.H"
#include "thread.H"
#include "waitbox.H"

class rpcserver;

template <typename> class orerror;
class rpcconn;

/* New servers are initially paused, and must be unpaused before they
   will start accepting incoming connections.  Attempts by remote
   peers to connect to a paused will block until it is unpaused
   (modulo the usual connect() timeouts). */
template <typename t> class pausedrpcserver {
    friend class rpcserver;
private: pausedthread<t> inner_;
private: pausedrpcserver(pausedthread<t> _inner) : inner_(_inner) {}
public:  t *go() { return inner_.go(); }
public:  void destroy() { inner_.destroy(); }
public:  t *unwrap() const { return inner_.unwrap(); } };

class rpcserver : protected thread {
    /* Each rpcserver has a dedicated thread which listens for
       incoming connections, spawns new connection threads, and teards
       down connection threads when they die. */
private:   void run(clientio);
private:   waitbox<bool> shutdown;

    /* What socket are we listening on? */
private:   listenfd sock;

private:   rpcserver(const rpcserver &) = delete;
private:   void operator=(const rpcserver &) = delete;

protected: rpcserver(constoken, listenfd);

    /* Create a new RPC server listening on a particular peername.  @t
       should be derived from rpcserver. */
public:    template <typename t, typename ... args>
    static orerror<pausedrpcserver<t> > listen(
        const peername &,
        args &...);

    /* We just accepted a new client and have a socket_t for them.
     * Turn that into a full rpcconn.  This runs in the main server
     * thread, so should not do anything which involves waiting for
     * the peer to do something.  The server implementation will call
     * ready() on the returned connection as soon as accept() finishes
     * (assuming that accept() succeeds) and the connection will then
     * start receiving incoming messages through the
     * rpcconn::message() interface. */
private:   virtual orerror<rpcconn *> accept(socket_t) = 0;

    /* Get a peername on with which other hosts can connect to this
     * server.  This will be either the name passed in to listen, if
     * that was a specific address, or something derived from it, if
     * it was a wildcard. */
public:    peername localname() const { return sock.localname(); }

    /* Stop accepting incoming connections, kill any outstanding ones,
     * and destroy the server. */
    /* Not a true destructor because it can wait for things. */
public:    virtual void destroy(clientio);

protected: virtual ~rpcserver() {}
};

#endif /* !RPCSERVER_H__ */
