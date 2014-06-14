#ifndef RPCCONN_H__
#define RPCCONN_H__

#include "buffer.H"
#include "fd.H"
#include "wireproto.H"

class beaconresult;
template <typename> class orerror;
class peername;
class tcpsocket;

class rpcconn {
private: buffer outgoing;
private: buffer incoming;
private: fd_t fd;
private: wireproto::sequencer sequencer;
private: list<const wireproto::rx_message *> pendingrx;

private: rpcconn() = delete;
private: rpcconn(const rpcconn &) = delete;
private: void operator=(const rpcconn &) = delete;

private: rpcconn(fd_t);

    /* Receive a message from the remote system, bypassing the
       incoming queue. */
private: orerror<const wireproto::rx_message *> _receive();

    /* Connect to a remote master server, using the results of the
       beacon protocol. */
public: static orerror<rpcconn *> connectmaster(const beaconresult &p);

    /* Raw connect: just connect to a given peername, without sending
     * a HELLO. */
public: static orerror<rpcconn *> connect(const peername &);

public: ~rpcconn();

    /* Send a message, without waiting for any kind of response.
     * Might block waiting for outgoing buffer space. */
public: maybe<error> send(const wireproto::tx_message &msg);

    /* Perform an RPC.  Send a given message and wait for a response
     * to come back.  This will receive any other messages which the
     * server deigns to send us and place them in the receive
     * queue. */
public: orerror<const wireproto::rx_message *> call(
    const wireproto::req_message &msg);

    /* Allocate a sequence number suitable for constructing a new
     * req_message */
public: wireproto::sequencenr allocsequencenr();

    /* Release a sequence number previously returned by
     * ::allocsequencenr() */
public: void putsequencenr(wireproto::sequencenr);
};

#endif
