#include <arpa/inet.h>

#include "peername.H"
#include "test2.H"

#include "parsers.tmpl"
#include "serialise.tmpl"
#include "test2.tmpl"

static testmodule __testpeername(
    "peername",
    list<filename>::mk("peername.C", "peername.H"),
    testmodule::LineCoverage(95_pc),
    testmodule::BranchCoverage(65_pc),
    "parser", [] { parsers::roundtrip(parsers::_peername()); },
    "status", [] {
        peername p((quickcheck()));
        assert(p.status() == p); },
    "udpbroadcast", [] {
        auto p(peername::udpbroadcast(peername::port(97)));
        auto sin = (const struct sockaddr_in *)p.sockaddr();
        assert(p.sockaddrsize() == sizeof(*sin));
        assert(sin->sin_port == htons(97));
        assert(sin->sin_addr.s_addr == 0xffffffff); },
    "all", [] {
        auto p(peername::all(peername::port::any));
        auto sin = (const struct sockaddr_in *)p.sockaddr();
        assert(p.sockaddrsize() == sizeof(*sin));
        assert(sin->sin_port == 0);
        assert(sin->sin_addr.s_addr == 0); },
    "loopback", [] {
        auto p(peername::loopback(peername::port::any));
        auto sin = (const struct sockaddr_in *)p.sockaddr();
        assert(p.sockaddrsize() == sizeof(*sin));
        assert(sin->sin_port == 0);
        assert(sin->sin_addr.s_addr == htonl(INADDR_LOOPBACK)); },
    "samehost", [] {
        for (unsigned x = 0; x < 100; x++) {
            peername p1((quickcheck()));
            assert(p1.samehost(p1)); }},
    "=", [] {
        peername p(peername::all(peername::port::any));
        peername q(peername::loopback(peername::port(73)));
        assert(!(p == q));
        p = q;
        assert(p == q); },
    "parseedge", [] {
        assert(parsers::_peername().match("ip://99999.1.2.3/")
               == error::noparse);
        assert(parsers::_peername().match("ip6://[::]:99999/")
               == error::noparse); },
    "peernameport", [] {
        parsers::roundtrip(parsers::_peernameport());
        quickcheck q;
        serialise<peername::port>(q);
        {   auto p(peername::loopback(peername::port(q)));
            assert(!p.isbroadcast());
            for (unsigned x = 0; x < 100; x++) {
                peername::port prt(q);
                assert(p.setport(prt).getport() == prt); } }
        {   struct sockaddr_in6 sin6;
            memset(&sin6, 0, sizeof(sin6));
            sin6.sin6_family = AF_INET6;
            peername p((struct sockaddr *)&sin6, sizeof(sin6));
            assert(!p.isbroadcast());
            for (unsigned x = 0; x < 100; x++) {
                peername::port prt(q);
                assert(p.setport(prt).getport() == prt); } } },
    "serialise", [] {
        quickcheck q;
        serialise<peername>(q); },
    "serialisebad", [] {
        {   ::buffer b;
            {   serialise1 s(b);
                s.push((unsigned)10000); }
            {   deserialise1 ds(b);
                peername ignore(ds);
                assert(ds.failure() == error::overflowed); } }
        {   ::buffer b;
            {   serialise1 s(b);
                s.push((unsigned)2);
                s.push((unsigned short)AF_INET); }
            {   deserialise1 ds(b);
                peername ignore(ds);
                assert(ds.failure() == error::invalidmessage); } }
        {   ::buffer b;
            {   serialise1 s(b);
                s.push((unsigned)2);
                s.push((unsigned short)AF_INET6); }
            {   deserialise1 ds(b);
                peername ignore(ds);
                assert(ds.failure() == error::invalidmessage); } } });
