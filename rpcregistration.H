#ifndef RPCREGISTRATION_H__
#define RPCREGISTRATION_H__

#include "wireproto.H"

template <typename> class rpcconnthread;
template <typename> class rpcinterface;
template <typename> class rpcservice;

template <typename cookie_t>
class rpcregistration {
    friend class rpcservice<cookie_t>;
    friend class rpcconnthread<cookie_t>;
private: mutex_t mux;
private: cond_t idle;
private: rpcservice<cookie_t> *owner;
private: list<rpcinterface<cookie_t> *> content;
private: unsigned outstanding;

private: rpcregistration() = delete;
private: rpcregistration(const rpcregistration &) = delete;
private: void operator=(const rpcregistration &) = delete;

private: rpcregistration(rpcservice<cookie_t> *_owner);
    /* Someone started using this interface.  Prevent it disappearing
     * until they're finished(). */
private: void start();
    /* Whoever called start() is now finished with the interface.
     * Wake up anyone waiting for the ref to drop away and potentially
     * tear the interface down. */
private: void finished();
    /* Prevent any further invocations of the registered interface or
       interfaces, Wait for any extant invocations to finish, and then
       release the rpcregistration structure. */
    /* Not a destructor because of non-trivial wait rules */
public:  void destroy();

private: ~rpcregistration(); };

#endif /* !RPCREGISTRATION_H__ */
