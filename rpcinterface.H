#ifndef RPCINTERFACE_H__
#define RPCINTERFACE_H__

#include "mutex.H"
#include "wireproto.H"

template <typename> class rpcconnthread;
template <typename> class rpcservice;

template <typename cookie_t>
class rpcinterface {
    friend class rpcconnthread<cookie_t>;
    friend class rpcservice<cookie_t>;
private:   wireproto::msgtag tag;
protected: rpcinterface(wireproto::msgtag _tag) : tag(_tag) {}
private:   virtual maybe<error> message(const wireproto::rx_message &,
                                        cookie_t cookie,
                                        buffer &,
                                        mutex_t::token /* called with
                                                        * rpcconn TX
                                                        * lock held */) = 0;
};

#endif /* !RPCINTERFACE_H__ */
