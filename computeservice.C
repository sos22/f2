#include <err.h>

#include "clustername.H"
#include "logging.H"
#include "nnp.H"
#include "parsers.H"
#include "rpcservice2.H"

#include "parsers.tmpl"
#include "rpcservice2.tmpl"

class computeservice : public rpcservice2 {
public: explicit computeservice(const constoken &token)
    : rpcservice2(token, interfacetype::compute) {}
public: orerror<void> called(clientio,
                             deserialise1 &,
                             interfacetype,
                             nnp<incompletecall>,
                             onconnectionthread) {
    return error::unrecognisedmessage; } };

int
main(int argc, char *argv[]) {
    initlogging("compute");
    initpubsub();

    if (argc != 3) {
        errx(1, "need two arguments, a cluster name and a slave name"); }
    auto cluster(parsers::__clustername()
                 .match(argv[1])
                 .fatal("parsing cluster name " + fields::mk(argv[1])));
    auto name(parsers::_slavename()
              .match(argv[2])
              .fatal("parsing slave name " + fields::mk(argv[2])));

    auto service(rpcservice2::listen<computeservice>(
                     clientio::CLIENTIO,
                     cluster,
                     name,
                     peername::all(peername::port::any))
                 .fatal("listening on computer interface"));

    while (true) timedelta::hours(1).future().sleep(clientio::CLIENTIO); }
