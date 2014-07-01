#ifndef RPCSERVER_H__
#define RPCSERVER_H__

#include "list.H"
#include "listenfd.H"
#include "mutex.H"
#include "peername.H"
#include "pubsub.H"
#include "thread.H"
#include "waitbox.H"

template <typename> class orerror;

template <typename conn_t>
class rpcserver : threadfn {
private:   class connsub : public subscription {
    public: conn_t *conn;
    public: connsub(subscriber &_sub, conn_t *_conn);
};

    /* Each rpcserver has a dedicated thread which listens for
       incoming connections, spawns new connection threads, and teards
       down connection threads when they die. */
private:   thread *thr;
private:   void run(clientio);
private:   waitbox<bool> shutdown;

    /* What socket are we listening on? */
private:   listenfd sock;

private:   rpcserver(const rpcserver &) = delete;
private:   void operator=(const rpcserver &) = delete;

protected: rpcserver();

    /* Start listening on a particular peer name.  Should be called by
     * derived classes early in the their startup sequence.  If this
     * fails then you should delete the server, rather than going
     * through the usual destroy() method. */
protected: maybe<error> listen(const peername &);

    /* We just accepted a new client and have a socket_t for them.
     * Turn that into a full conn_t.  This runs in the main server
     * thread, so should not do anything which involves waiting for
     * the peer to do something. */
private:   virtual orerror<conn_t *> accept(socket_t) = 0;

    /* Get a peername on with which other hosts can connect to this
     * server.  This will be either the name passed in to listen, if
     * that was a specific address, or something derived from it, if
     * it was a wildcard. */
public:    peername localname() const { return sock.localname(); }

    /* Stop accepting incoming connections, kill any outstanding ones,
     * and destroy the server. */
    /* Not a true destructor because it can wait for things. */
public:    virtual void destroy(clientio);

    /* Only called from destroy() method and when listen() fails. */
protected: virtual ~rpcserver();
};

#endif /* !RPCSERVER_H__ */
