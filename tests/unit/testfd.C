#include <sys/poll.h>
#include <unistd.h>

#include "fd.H"
#include "test2.H"
#include "timedelta.H"

#include "test2.tmpl"

static testmodule __testfd(
    "fd",
    list<filename>::mk("fd.C", "fd.H"),
    testmodule::LineCoverage(85_pc),
    testmodule::BranchCoverage(35_pc),
    "closepoll", [] {
        int p[2];
        int r(::pipe(p));
        assert(r >= 0);
        ::close(p[1]);
        fd_t fd(p[0]);
        assert(fd.poll(POLLIN).revents == 0);
        assert((fd.poll(POLLIN).events & POLLIN) != 0);
        assert((fd.poll(POLLIN).events & POLLOUT) == 0);
        assert((fd.poll(POLLOUT).events & POLLIN) == 0);
        assert((fd.poll(POLLOUT).events & POLLOUT) != 0);
        assert(fd.poll(POLLIN).fd == p[0]);
        assert(fd.polled(fd.poll(POLLIN)));
        fd.close();
        assert(write(p[0], "foo", 3) == -1);
        assert(errno == EBADF); },
    "readwrite", [] {
        auto r(fd_t::pipe().fatal("pipe"));
        {   auto s(r.write.write(clientio::CLIENTIO, "X", 1));
            assert(s.success() == 1); }
        {   char buf[] = "Hello";
            auto s(r.read.read(clientio::CLIENTIO, buf, 5));
            assert(s.success() == 1);
            assert(!strcmp(buf, "Xello")); }
        r.read.close();
        r.write.close(); },
    "readtimeout", [] {
        auto r(fd_t::pipe().fatal("pipe"));
        char buf[] = "Hello";
        auto s(r.read.read(
                   clientio::CLIENTIO,
                   buf,
                   5,
                   timestamp::now() + timedelta::milliseconds(5)));
        assert(s == error::timeout);
        assert(!strcmp(buf, "Hello"));
        r.read.close();
        r.write.close(); },
    "readerr", [] {
        auto r(fd_t::pipe().fatal("pipe"));
        char buf[] = "Hello";
        r.read.close();
        auto s(r.read.read(
                   clientio::CLIENTIO,
                   buf,
                   5));
        assert(s == error::from_errno(EBADF));
        assert(!strcmp(buf, "Hello"));
        r.write.close(); },
    "readdisconnected", [] {
        auto r(fd_t::pipe().fatal("pipe"));
        char buf[] = "Hello";
        r.write.close();
        auto s(r.read.read(
                   clientio::CLIENTIO,
                   buf,
                   5));
        assert(s == error::disconnected);
        assert(!strcmp(buf, "Hello"));
        r.read.close(); },
    "writeerr", [] {
        auto r(fd_t::pipe().fatal("pipe"));
        char buf[] = "Hello";
        r.write.close();
        auto s(r.write.write(
                   clientio::CLIENTIO,
                   buf,
                   5));
        assert(s == error::from_errno(EBADF));
        r.read.close(); },
    "writedisconnected", [] {
        auto r(fd_t::pipe().fatal("pipe"));
        char buf[] = "Hello";
        r.read.close();
        auto s(r.write.write(
                   clientio::CLIENTIO,
                   buf,
                   5));
        assert(s == error::disconnected);
        r.write.close(); },
    "writetimeout", [] {
        auto r(fd_t::pipe().fatal("pipe"));
        while (true) {
            char buf[] = "Hello";
            auto s(r.write.write(
                       clientio::CLIENTIO,
                       buf,
                       sizeof(buf),
                       timestamp::now() + timedelta::milliseconds(5)));
            if (s == error::timeout) break;
            assert(s.success() == sizeof(buf)); }
        r.read.close();
        r.write.close(); },
    "nonblockread", [] {
        auto r(fd_t::pipe().fatal("pipe"));
        char buf[] = "Hello";
        r.read.nonblock(true).fatal("nonblock");
        auto s(r.read.read(
                   clientio::CLIENTIO,
                   buf,
                   5));
        assert(s == error::wouldblock);
        assert(!strcmp(buf, "Hello"));
        r.read.close();
        r.write.close(); },
    "nonblockwrite", [] {
        auto r(fd_t::pipe().fatal("pipe"));
        r.write.nonblock(true).fatal("nonblock");
        while (true) {
            char buf[] = "Hello";
            auto s(r.write.write(
                       clientio::CLIENTIO,
                       buf,
                       sizeof(buf)));
            if (s == error::wouldblock) break;
            assert(s.success() == sizeof(buf)); }
        r.close(); },
    "status", [] {
        auto r(fd_t::pipe().fatal("pipe"));
        fields::print(fields::mk(r.read.status()) + "\n");
        fields::print(fields::mk(r.write.status()) + "\n"); },
    "dup2", [] {
        auto r(fd_t::pipe().fatal("pipe"));
        r.write.write(clientio::CLIENTIO, "HELLO", 5);
        auto r2(fd_t::pipe().fatal("pipe"));
        r2.write.close();
        r.read.dup2(r2.read).fatal("dup2");
        r.read.close();
        char buf[5];
        auto r3(r2.read.read(clientio::CLIENTIO, buf, 5));
        assert(r3.success() == 5);
        r2.read.close();
        r.write.close(); },
    "field", [] { assert(!strcmp(fields::mk(fd_t(7)).c_str(), "fd:7")); },
    "writefield", [] {
        auto r(fd_t::pipe().fatal("pipe"));
        r.write.write(clientio::CLIENTIO, fields::mk(7) + "...")
            .fatal("write");
        r.write.close();
        char b[10];
        assert(r.read.read(clientio::CLIENTIO, b, sizeof(b)) == 4);
        assert(!memcmp(b, "7...", 4));
        r.read.close(); },
    "badstatus", [] {
        auto r(fd_t::pipe().fatal("pipe"));
        r.close();
        fields::print(fields::mk(r.read.status()) + "\n"); },
    "readpoll", [] {
        auto r(fd_t::pipe().fatal("pipe"));
        char buf[10];
        assert(r.read.readpoll(buf, sizeof(buf)) == error::timeout);
        r.write.write(clientio::CLIENTIO, "HELLO", 6)
            .fatal("pipe write");
        assert(r.read.readpoll(buf, sizeof(buf)) == 6);
        assert(!strcmp(buf, "HELLO"));
        r.close(); });
