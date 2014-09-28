#include "rpctest.H"

#include "peername.H"
#include "proto.H"
#include "rpcclient.H"
#include "rpcservice.H"
#include "test.H"
#include "timedelta.H"

#include "rpcservice.tmpl"

namespace tests {

class trivserver : public rpcservice {
public:  trivserver(const rpcservice::constoken &t)
    : rpcservice(t) {}
private: void call(const wireproto::rx_message &, response *resp) {
    resp->fail(error::unrecognisedmessage); } };

void _rpc() {
    testcaseIO("rpc", "basic", [] (clientio io) {
            auto s(rpcservice::listen<trivserver>(
                       peername::loopback(peername::port::any))
                   .fatal("starting trivial server"));
            auto r(s->localname());
            auto c(rpcclient::connect(io, r)
                   .fatal("connecting to trivial server"));
            delete c->call(io, wireproto::req_message(proto::PING::tag))
                .fatal("pinging trivial server");
            assert(c->call(io, wireproto::req_message(wireproto::msgtag(93)))
                   == error::unrecognisedmessage);
            auto c2(rpcclient::connect(io, r)
                   .fatal("connecting to trivial server"));
            delete c2->call(io, wireproto::req_message(proto::PING::tag))
                .fatal("pinging trivial server");
            assert(c2->call(io, wireproto::req_message(wireproto::msgtag(93)))
                   == error::unrecognisedmessage);
            delete c;
            (timestamp::now() + timedelta::milliseconds(100)).sleep(io);
            delete c2;
            s->destroy();
            assert(rpcclient::connect(io, r) ==
                   error::from_errno(ECONNREFUSED)); });
    testcaseV("rpc", "listenbad", [] {
            auto s(rpcservice::listen<trivserver>(
                       peername::loopback(peername::port::any))
                   .fatal("starting trivial server"));
            assert(rpcservice::listen<trivserver>(
                       peername::loopback(s->localname().getport())) ==
                   error::from_errno(EADDRINUSE));
            s->destroy(); }); } }
