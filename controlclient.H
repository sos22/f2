#ifndef CONTROLCLIENT_H__
#define CONTROLCLIENT_H__

#include "buffer.H"
#include "fd.H"
#include "orerror.H"
#include "wireproto.H"

class controlclient {
    controlclient() = delete;
public:
    /* Connect the local master control interface and return a new
     * control client. */
    static orerror<controlclient *> connect();
    /* Disconnect and tear down a control client.  Any messages
       received but not returned from ::receive() are discarded. */
    void destroy() const;
    /* Send a message over the control interface, blocking if
     * necessary. */
    maybe<error> send(const wireproto::tx_message &what);
    /* Receive the next message out of the queue, waiting if the queue
     * is currently empty.  Messages can be re-ordered and new async
     * status messages inserted at any time. */
    orerror<const wireproto::rx_message *> receive();
    /* Perform an RPC.  Send a given message and wait for a response
     * to come back.  This will receive any other messages which the
     * server deigns to send us and place them in the receive
     * queue. */
    orerror<const wireproto::rx_message *> call(const wireproto::tx_message &msg);
};

#endif /* !CONTROLCLIENT_H__ */
