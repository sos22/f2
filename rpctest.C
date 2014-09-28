#include "rpctest.H"

#include "peername.H"
#include "proto.H"
#include "rpcclient.H"
#include "rpcservice.H"
#include "test.H"

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
            auto c(rpcclient::connect(io, s->localname())
                   .fatal("connecting to trivial server"));
            delete c->call(io, wireproto::req_message(proto::PING::tag))
                .fatal("pinging trivial server");
            delete c;
            s->destroy(); }); } }
