#ifndef RPCINTERFACE_H__
#define RPCINTERFACE_H__

#include "mutex.H"
#include "wireproto.H"

template <typename> class rpcconnthread;
template <typename> class rpcservice;

class messageresult {
private: orerror<maybe<wireproto::tx_message *> > const content;
private: messageresult() : content(Nothing) {}
public:  messageresult(error e) : content(e) {}
public:  messageresult(wireproto::tx_message *m)
    : content(m) { assert(m); }
public:  static messageresult noreply;
public:  bool isfailure() const { return content.isfailure(); }
public:  error failure() const { return content.failure(); }
public:  bool isreply() const {
    return content.issuccess() && content.success().isjust(); }
public:  wireproto::tx_message *reply() const {
    assert(content.issuccess());
    assert(content.success().isjust());
    return content.success().just(); } };

template <typename cookie_t>
class rpcinterface {
    friend class rpcconnthread<cookie_t>;
    friend class rpcservice<cookie_t>;
private:   wireproto::msgtag tag;
protected: rpcinterface(wireproto::msgtag _tag) : tag(_tag) {}
private:   virtual messageresult message(
    const wireproto::rx_message &,
    cookie_t cookie) = 0;
};

#endif /* !RPCINTERFACE_H__ */
