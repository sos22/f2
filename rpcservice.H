#ifndef RPCSERVICE_H__
#define RPCSERVICE_H__

/* An RPC service represents a collection of RPC interfaces which are
   exported over the same socket. */
template <typename cookie_t>
class rpcservice {
    friend class rpcregistration<cookie_t>;
private:   class unknowniface : public rpcinterface<cookie_t> {
    public:  unknowniface();
    private: maybe<error> message(const wireproto::rx_message &,
                                  cookie_t cookie,
                                  buffer &);
    };
private:   mutex_t mux;
private:   list<rpcregistration<cookie_t> *> registrations;
private:   unknowniface unknowninterface;
private:   rpcregistration<cookie_t> *const unknownregistration;
private:   rpcservice(const rpcservice &) = delete;
private:   void operator=(const rpcservice &) = delete;
    
    /* Construct a new empty service.  Until some interfaces are
       registered, the service returns unrecognisedmessage for every
       request. */
public:    rpcservice();

    /* Register a new RPC interface with the service.  The interface
       can be invoked at any time after this is called until it us
       unregistered.  It is an error to register multiple interfaces
       with the same message tag.  Interfaces can be deregistered with
       the rpcregistration::deregister() method. */
public:    rpcregistration<cookie_t> *registeriface(rpcinterface<cookie_t> &ri);
    /* Register a group of RPC interfaces in one go.  Note that
       registration is not guaranteed to be atomic with respect to
       incoming calls i.e. if registering interfaces A and B at once
       there may be a window in which A is accessible and B not or
       vice versa.  Interfaces which are registered as a group must be
       deregistered as a grouop (although, as with registation, there
       is no guarantee that deregistration happens atomically). */
public:    class multiregistration {
        friend class rpcservice<cookie_t>;
    private: multiregistration(const multiregistration &) = delete;
    private: void operator=(const multiregistration &) = delete;
    private: list<rpcinterface<cookie_t> *> content;
    public:  multiregistration() : content() {};
    public:  multiregistration &add(rpcinterface<cookie_t> &);
    public:  ~multiregistration();
};
public:    rpcregistration<cookie_t> *registeriface(const multiregistration &r);

    /* Lookup the registered interface for a given tag.  Returns the
       registration and the interface (which might be the unknown
       interface for unrecognised tags).  The registration is returned
       with its reference count incremented; call reg->finish() once
       you're done with it. */
    struct lookupres {
        rpcregistration<cookie_t> *reg;
        rpcinterface<cookie_t> *iface; };
public:    lookupres lookup(wireproto::msgtag);

    /* Remove a registration from the list, preventing any further
       invocations, without waiting for extant invocations to finish.
       Should only be invoked by rpcregistration. */
private:   void deregister(rpcregistration<cookie_t> *);

    /* Destroy the service, unregistering any remaining interfaces.
     * Waits for any extant invocations on the service to finish. */
    /* Not a true destructor because it has non-trivial wait rules. */
public:    void destroy();

private:   ~rpcservice();
};

#endif /* !RPCSERVICE_H__ */
