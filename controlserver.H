#ifndef CONTROLSERVER_H__
#define CONTROLSERVER_H__

#include "wireproto.H"

class buffer;
class error;
template <typename t> class list;
template <typename> class maybe;
template <typename t> class orerror;
class peername;
class shutdowncode;
template <typename t> class waitbox;

namespace _controlserver {
class controlserverimpl;
class controlregistration;
}

class controliface {
protected:
    controliface(wireproto::msgtag _tag)
        : tag(_tag)
        {}
public:
    const wireproto::msgtag tag;
    virtual maybe<error> controlmessage(const wireproto::rx_message &,
                                        buffer &) = 0;
    virtual ~controliface() {}
};

class controlserver {
    controlserver() = delete;
    controlserver(const controlserver &) = delete;
    void operator=(const controlserver &) = delete;
public:
    /* Create a new control server and start serving requests.  The
       server will put a shutdown code in @s when it receives a QUIT
       message, but will continue serving incoming requests until you
       call destroy(). */
    static orerror<controlserver *> build(const peername &listenon,
                                          waitbox<shutdowncode> *s);
    /* Release the control server interface, stopping it first if
     * necessary. */
    void destroy();

    class iface;
    /* Register a new control interface message type.  Returns a
       cookie which can later be used to deregister it.  Aborts if the
       same interface is registered multiple times, or if multiple
       interfaces are registered with the same tag. */
    iface registeriface(controliface &iface);

    /* Convenience interface for registering lots of control
       interfaces at once. */
    class multiregistration {
        friend class _controlserver::controlserverimpl;
        list<controliface *> content;
    public:
        multiregistration() : content() {}
        multiregistration &add(controliface &iface);
        ~multiregistration() { content.flush(); }
    };
    iface registeriface(const multiregistration &);

    class iface {
        _controlserver::controlregistration *content;
        iface() = delete;
        iface(_controlserver::controlregistration *_content)
            : content(_content)
            {}
    public:
        /* Because I can't figure out how to make this class friends
         * with registeriface() */
        static iface __mk_iface__(
            _controlserver::controlregistration *content) {
                return iface(content); }
        iface(const iface &o)
            : content(o.content) {}
        void operator=(const iface &o) { content = o.content; }

        /* Deregister the interface message type.  This waits for all
           outstanding invocations of the interface to finish before
           returning. */
        void deregister();
    };
};

#endif /* !CONTROLSERVER_H__ */
