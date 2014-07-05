#include "fd.H"

#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "fields.H"
#include "maybe.H"
#include "proto.H"
#include "timedelta.H"

#include "wireproto.tmpl"

#include "fieldfinal.H"

void
fd_t::close(void) const
{
    ::close(fd);
}

struct pollfd
fd_t::poll(int mode) const
{
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = mode;
    pfd.revents = 0;
    return pfd;
}

bool
fd_t::polled(const struct pollfd &pfd) const
{
    return pfd.fd == fd;
}

orerror<size_t>
fd_t::read(clientio, void *buf, size_t sz, maybe<timestamp> deadline) const {
    if (deadline != Nothing) {
        while (1) {
            struct pollfd pfd = poll(POLLIN);
            auto remaining(
                (deadline.just() - timestamp::now()).as_milliseconds());
            if (remaining < 0) return error::timeout;
            int r = ::poll(&pfd, 1, remaining);
            if (r < 0) return error::from_errno();
            if (r == 1) break;
            assert(r == 0); } }
    auto s(::read(fd, buf, sz));
    if (s < 0)
        return error::from_errno();
    else if (s == 0)
        return error::disconnected;
    else
        return s;
}

orerror<size_t>
fd_t::write(clientio,
            const void *buf,
            size_t sz,
            maybe<timestamp> deadline) const {
    if (deadline != Nothing) {
        while (1) {
            struct pollfd pfd = poll(POLLOUT);
            auto remaining(
                (deadline.just() - timestamp::now()).as_milliseconds());
            int r = ::poll(&pfd, 1, remaining);
            if (r < 0) return error::from_errno();
            if (r == 1) break;
            assert(r == 0); } }
    auto s(::write(fd, buf, sz));
    if (s < 0)
        return error::from_errno();
    else if (s == 0)
        return error::disconnected;
    else
        return s; }

orerror<fd_t::piperes>
fd_t::pipe()
{
    int fds[2];
    if (::pipe(fds) < 0)
        return error::from_errno();
    piperes r;
    r.read = fd_t(fds[0]);
    r.write = fd_t(fds[1]);
    return r;
}

const fields::field &
fields::mk(const fd_t &fd)
{
    return "fd:" + fields::mk(fd.fd).nosep();
}

fd_t::status_t
fd_t::status() const {
    struct timeval lastrx;
    maybe<struct timeval> lrx = Nothing;
    if (ioctl(fd, SIOCGSTAMP, &lastrx) >= 0) lrx = lastrx;
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = ~0;
    pfd.revents = 0;
    maybe<int> revents = Nothing;
    if (::poll(&pfd, 1, 0) >= 0) revents = pfd.revents;
    return status_t(fd, lrx, revents); }

wireproto_wrapper_type(fd_t::status_t)
void
fd_t::status_t::addparam(
    wireproto::parameter<fd_t::status_t> tmpl,
    wireproto::tx_message &out) const {
    wireproto::tx_compoundparameter p;
    p.addparam(proto::fd_tstatus::fd, fd);
    if (lastrx.isjust()) p.addparam(proto::fd_tstatus::lastrx, lastrx.just());
    if (revents.isjust()) {
        p.addparam(proto::fd_tstatus::revents, revents.just()); }
    out.addparam(
        wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
        p); }
maybe<fd_t::status_t>
fd_t::status_t::getparam(
    wireproto::parameter<fd_t::status_t> tmpl,
    const wireproto::rx_message &rxm) {
    auto packed(rxm.getparam(
                    wireproto::parameter<wireproto::rx_message>(tmpl)));
    if (!packed) return Nothing;
    auto &p(packed.just());
    auto fd(p.getparam(proto::fd_tstatus::fd));
    auto lastrx(p.getparam(proto::fd_tstatus::lastrx));
    auto revents(p.getparam(proto::fd_tstatus::revents));
    if (!fd) return Nothing;
    return fd_t::status_t(fd.just(), lastrx, revents); }
const fields::field &
fields::mk(const fd_t::status_t &o) {
    return "<fd:" + mk(o.fd) +
        " lastrx:" + mk(o.lastrx) +
        " revents:" + (o.revents.isjust()
                       ? mk(o.revents.just()).base(2)
                       : mk(o.revents)) +
        ">"; }

namespace fields {
template const field &mk(const maybe<timeval> &);
}
