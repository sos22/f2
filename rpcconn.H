#ifndef RPCCONN_H__
#define RPCCONN_H__

#include "buffer.H"
#include "either.H"
#include "mutex.H"
#include "peername.H"
#include "socket.H"
#include "wireproto.H"

class beaconresult;
template <typename> class orerror;
template <typename> class rpcserver;
class subscriber;
class subscriptionbase;
class tcpsocket;

class rpcconnstatus {
public: buffer::status_t outgoing;
public: buffer::status_t incoming;
public: fd_t::status_t fd;
public: wireproto::sequencer::status_t sequencer;
public: list<wireproto::rx_message::status_t> pendingrx;
public: peername::status_t peername_;
public: rpcconnstatus(const buffer::status_t &_outgoing,
                      const buffer::status_t &_incoming,
                      const fd_t::status_t &_fd,
                      const wireproto::sequencer::status_t &_sequencer,
                      const list<wireproto::rx_message::status_t> &_pendingrx,
                      const peername::status_t &_peername)
    : outgoing(_outgoing),
      incoming(_incoming),
      fd(_fd),
      sequencer(_sequencer),
      pendingrx(_pendingrx),
      peername_(_peername) {}
public: ~rpcconnstatus() { pendingrx.flush(); }
WIREPROTO_TYPE(rpcconnstatus);
};
namespace fields {
const field &mk(const rpcconnstatus &);
}

class rpcconn {
    template <typename t> friend class rpcconnthread;
private: mutex_t txlock;
private: buffer outgoing;
private: mutable mutex_t rxlock;
private: buffer incoming;
private: socket_t fd;
private: mutex_t sequencerlock;
private: wireproto::sequencer sequencer;
private: list<const wireproto::rx_message *> pendingrx;
private: const peername peer_;

private: rpcconn() = delete;
private: rpcconn(const rpcconn &) = delete;
private: void operator=(const rpcconn &) = delete;

private: rpcconn(socket_t, const peername &);

    class receiveres {
    private: either<subscriptionbase *, const wireproto::rx_message *> content;
    public:  bool issubscription() const { return content.isleft(); }
    public:  const wireproto::rx_message *message() const {
        return content.right(); }
    public:  bool ismessage() const { return content.isright(); }
    public:  subscriptionbase *subscription() const {
        return content.left(); }
    public:  receiveres(const wireproto::rx_message *m)
        : content(m) {}
    public:  receiveres(subscriptionbase *m)
        : content(m) {}
    };

    /* Receive a message from the remote system, bypassing the
       incoming queue. */
private: orerror<receiveres> _receive(
    clientio,
    mutex_t::token,
    subscriber &sub,
    maybe<timestamp> deadline = Nothing);

    /* Receive a message from the incoming queue, if it's non-empty,
       or from the remote system, otherwise. */
public: orerror<const wireproto::rx_message *> receive(
    clientio,
    maybe<timestamp> deadline = Nothing);

    /* Receive the reply to a message with a particular sequence
       number while also waiting for events on a particular
       subscriber. */
public: orerror<receiveres> receive(clientio,
                                    subscriber &,
                                    wireproto::sequencenr,
                                    maybe<timestamp> deadline = Nothing);

    /* Connect to a remote master server, using the results of the
       beacon protocol. */
public: static orerror<rpcconn *> connectmaster(
    clientio,
    const beaconresult &p);

    /* Raw connect: just connect to a given peername, without sending
     * a HELLO. */
public: static orerror<rpcconn *> connect(
    clientio,
    const peername &);

public: ~rpcconn();

    /* Send a message, without waiting for any kind of response.
     * Might block waiting for outgoing buffer space. */
public: maybe<error> send(
    clientio,
    const wireproto::tx_message &msg,
    maybe<timestamp> deadline = Nothing);

    /* Perform an RPC.  Send a given message and wait for a response
     * to come back.  This will receive any other messages which the
     * server deigns to send us and place them in the receive
     * queue. */
public: orerror<const wireproto::rx_message *> call(
    clientio,
    const wireproto::req_message &msg);

    /* Allocate a sequence number suitable for constructing a new
     * req_message */
public: wireproto::sequencenr allocsequencenr();

    /* Release a sequence number previously returned by
     * ::allocsequencenr() */
public: void putsequencenr(wireproto::sequencenr);
    
    /* Get a peername for the thing on the other end of the
     * connection. */
public: peername peer() const;

public: typedef rpcconnstatus status_t;
public: status_t status(
    maybe<mutex_t::token> coordinatorlock) const;
};

#endif
