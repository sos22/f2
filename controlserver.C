#include "controlserver.H"

#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include "buffer.H"
#include "cond.H"
#include "fd.H"
#include "list.H"
#include "listenfd.H"
#include "logging.H"
#include "maybe.H"
#include "mutex.H"
#include "thread.H"
#include "orerror.H"
#include "proto.H"
#include "shutdown.H"
#include "unixsocket.H"
#include "waitbox.H"
#include "waitqueue.H"
#include "wireproto.H"

#include "list.tmpl"
#include "waitqueue.tmpl"

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

namespace cf {
class controlserver;

struct registration {
    registration() __attribute__((noreturn)); /* should never be
                                               * called, but needed
                                               * for linked list
                                               * templates */
    registration(const registration &o);
    void operator=(const registration &) = delete;

    controlserver *server;
    wireproto::msgtag tag;
    unsigned outstanding;
    controliface &iface;
    cond_t idle;
    registration(controlserver *_server,
                 wireproto::msgtag _tag,
                 controliface &_iface);
    void deregister() const;
    ~registration() { assert(outstanding == 0); }
};

class pingiface : public controliface {
public:
    maybe<error> controlmessage(const wireproto::rx_message &, buffer &);
};
class unknowniface : public controliface {
public:
    maybe<error> controlmessage(const wireproto::rx_message &, buffer &);
};
class quitiface : public controliface {
    waitbox<shutdowncode> *s;
    quitiface() = delete;
    quitiface(const quitiface &) = delete;
    void operator=(const quitiface &) = delete;
public:
    quitiface(waitbox<shutdowncode> *_s)
        : s(_s)
        {}
    maybe<error> controlmessage(const wireproto::rx_message &, buffer &);
};

struct controlserver : threadfn {
    class clientthread : threadfn {
        controlserver *owner;
        fd_t fd;
        thread *_thr;
        waitbox<bool> *shutdown;
        void run(void);
        bool runcommand(buffer &incoming,
                        buffer &outgoing,
                        bool *die);
        clientthread(controlserver *_owner,
                     fd_t _fd,
                     waitbox<bool> *_shutdown)
            : owner(_owner), fd(_fd), _thr(NULL),
              shutdown(_shutdown)
            {}
        clientthread(const clientthread &) = delete;
        void operator=(const clientthread &) = delete;
    public:
        static orerror<clientthread *> spawn(controlserver *,
                                             fd_t,
                                             waitbox<bool> *);
        thread *thr() { return _thr; }
        virtual ~clientthread() {}
    };
    list<registration *> registrations;
    waitbox<bool> *localshutdown;
    waitbox<shutdowncode> *globalshutdown;
    mutex_t mux;
    waitqueue<clientthread *> *dying;
    thread *t;
    listenfd sock;

    unknowniface unknowninterface;
    pingiface pinginterface;
    quitiface quitinterface;
    registration *unknownregistration;
    registration *loggingregistration;
    registration *pingregistration;
    registration *quitregistration;

    void run();
    controlserver(waitbox<shutdowncode> *_s)
        : registrations(),
          localshutdown(NULL),
          globalshutdown(_s),
          mux(),
          dying(),
          t(NULL),
          sock(),
          unknowninterface(),
          pinginterface(),
          quitinterface(_s),
          unknownregistration(registeriface(wireproto::msgtag(0),
                                            unknowninterface)),
          loggingregistration(registeriface(proto::GETLOGS::tag,
                                            getlogsiface::singleton)),
          pingregistration(registeriface(proto::PING::tag,
                                         pinginterface)),
          quitregistration(registeriface(proto::QUIT::tag,
                                         quitinterface))
        {
        }
    controlserver(const controlserver &) = delete;
    void operator=(const controlserver &) = delete;

    registration *registeriface(wireproto::msgtag,
                                controliface &iface);

    maybe<error> setup();
    void end();
    ~controlserver()
        {
            unknownregistration->deregister();
            loggingregistration->deregister();
            pingregistration->deregister();
            quitregistration->deregister();
            assert(!t);
            localshutdown->destroy();
            dying->destroy();
        }
};
static controlserver *
rc(::controlserver *c)
{
    return (controlserver *)c;
}

registration::registration(controlserver *_server,
                           wireproto::msgtag _tag,
                           controliface &_iface)
    : server(_server),
      tag(_tag),
      outstanding(0),
      iface(_iface),
      idle(server->mux)
{}

registration::registration(const registration &o)
    : server(o.server),
      tag(o.tag),
      outstanding(0),
      iface(o.iface),
      idle(server->mux)
{
    assert(!o.outstanding);
}

registration::registration()
    : server(NULL),
      tag(0),
      outstanding(0),
      iface(*(controliface *)NULL),
      idle(server->mux)
{
    abort();
}

void
registration::deregister() const
{
    auto token(server->mux.lock());
    for (auto it(server->registrations.start()); !it.finished(); it.next()) {
        if (*it == this) {
            it.remove();
            while (outstanding > 0)
                token = idle.wait(&token);
            server->mux.unlock(&token);
            delete this;
            return;
        }
    }
    server->mux.unlock(&token);
    abort();
}

registration *
controlserver::registeriface(wireproto::msgtag tag, controliface &iface)
{
    auto reg(new registration(this, tag, iface));
    auto token(mux.lock());
    for (auto it(registrations.start()); !it.finished(); it.next()) {
        if ((*it)->tag == tag) {
            mux.unlock(&token);
            abort();
        }
    }
    registrations.pushtail(reg);
    mux.unlock(&token);
    return reg;
}

void
controlserver::clientthread::run()
{
    struct pollfd fds[2];

    fds[0] = shutdown->fd().poll(POLLIN);
    fds[1] = fd.poll(POLLIN);

    buffer outgoing;
    buffer incoming;
    bool die;
    die = false;
    logmsg(loglevel::info, "start client thread");
    while (!die && !shutdown->ready()) {
        auto r(::poll(fds, 2, -1));
        if (r < 0)
            error::from_errno().fatal("poll client");
        if (fds[0].revents) {
            assert(fds[0].revents == POLLIN);
            assert(shutdown->ready());
            fds[0].revents = 0;
        }
        if (fds[1].revents) {
            if (fds[1].revents & POLLOUT) {
                auto rr(outgoing.send(fd));
                if (rr.isjust()) {
                    rr.just().warn("sending to client");
                    break;
                }
                fds[1].revents &= ~POLLOUT;
                if (outgoing.empty())
                    fds[1].events &= ~POLLOUT;
            }
            if (fds[1].revents & POLLIN) {
                auto t(incoming.receive(fd));
                if (t.isjust()) {
                    t.just().warn("receiving from client");
                    break;
                }
                fds[1].revents &= ~POLLIN;
                while (runcommand(incoming, outgoing, &die))
                    ;
                if (!outgoing.empty())
                    fds[1].events |= POLLOUT;
            }
        }
    }
    logmsg(loglevel::info, "client thread finishing");
    fd.close();
    auto token(owner->mux.lock());
    owner->dying->pushtail(this);
    owner->mux.unlock(&token);
}

bool
controlserver::clientthread::runcommand(buffer &incoming,
                                        buffer &outgoing,
                                        bool *die)
{
    auto r(wireproto::rx_message::fetch(incoming));
    if (r.isfailure()) {
        *die = r.failure() != error::underflowed;
        return false;
    }
    assert(r.success() != NULL);
    auto tag(r.success()->t);
    logmsg(loglevel::verbose, "run command %d\n", tag.as_int());
    registration *handler;

    auto token(owner->mux.lock());
    handler = owner->unknownregistration;
    for (auto it(owner->registrations.start());
         !it.finished();
         it.next()) {
        if ((*it)->tag == tag) {
            handler = *it;
            break;
        }
    }
    handler->outstanding++;
    owner->mux.unlock(&token);

    auto res(handler->iface.controlmessage(*r.success(), outgoing));

    auto token2(owner->mux.lock());
    handler->outstanding--;
    if (!handler->outstanding)
        handler->idle.broadcast(token2);
    owner->mux.unlock(&token2);

    if (res.isjust())
        wireproto::err_resp_message(*r.success(), res.just())
            .serialise(outgoing);
    r.success()->finish();
    return true;
}

orerror<controlserver::clientthread *>
controlserver::clientthread::spawn(controlserver *server,
                                   fd_t fd,
                                   waitbox<bool> *shutdown)
{
    auto work(new clientthread(server, fd, shutdown));
    auto tr(thread::spawn(work, &work->_thr));
    if (tr.isjust()) {
        delete work;
        return tr.just();
    } else {
        return work;
    }
}

maybe<error>
pingiface::controlmessage(const wireproto::rx_message &msg, buffer &outgoing)
{
    auto payload(msg.getparam(proto::PING::req::msg));
    if (payload.isjust())
        logmsg(loglevel::info, "ping msg %s", payload.just());
    else
        logmsg(loglevel::failure, "ping with no message?");
    wireproto::resp_message m(msg);
    static int cntr;
    m.addparam(proto::PING::resp::cntr, cntr++);
    m.addparam(proto::PING::resp::msg, "response message");
    auto r(m.serialise(outgoing));
    if (r.isjust())
        r.just().warn("sending pong");
    return r;
}

maybe<error>
quitiface::controlmessage(const wireproto::rx_message &msg, buffer &)
{
    auto reason(msg.getparam(proto::QUIT::req::reason));
    if (!reason) return error::missingparameter;
    auto message(msg.getparam(proto::QUIT::req::message));
    logmsg(loglevel::notice,
           "received a quit message: %s",
           message.isjust()
           ? message.just()
           : "<no explanation given>");
    s->set(reason.just());
    return Nothing;
}

maybe<error>
unknowniface::controlmessage(const wireproto::rx_message &msg, buffer &)
{
    logmsg(loglevel::failure,
           "Received an unrecognised message type %d",
           msg.t.as_int());
    return error::unrecognisedmessage;
}

void
controlserver::run()
{
    struct pollfd pfds[3];
    list<clientthread *> threads;
    int nrfds;
    pfds[0] = localshutdown->fd().poll(POLLIN);
    pfds[1] = dying->fd().poll(POLLIN);
    pfds[2] = sock.poll();
    nrfds = 3;
    while (1) {
        int r = poll(pfds, nrfds, -1);
        if (r < 0)
            error::from_errno().fatal("polling control interface");
        for (unsigned i = 0; r; i++) {
            assert(i < ARRAY_SIZE(pfds));
            if (pfds[i].revents) {
                if (localshutdown->fd().polled(pfds[i])) {
                    logmsg(loglevel::info,
                           "control interface received local shutdown");
                    assert(!(pfds[i].revents & POLLERR));
                    memmove(pfds+i, pfds+i+1, sizeof(pfds[0]) * (nrfds-i-1));
                    nrfds--;
                    /* No longer interested in accepting more clients. */
                    for (int j = 0; j < nrfds; j++) {
                        if (sock.polled(pfds[j])) {
                            memmove(pfds + j,
                                    pfds + j + 1,
                                    sizeof(pfds[0]) * (nrfds - j - 1));
                            nrfds--;
                            j--;
                        }
                    }
                    if (threads.empty())
/**/                    goto out;
                } else if (dying->fd().polled(pfds[i])) {
                    assert(!(pfds[i].revents & POLLERR));
                    auto thr(dying->pophead());
                    thr->thr()->join();
                    for (auto it(threads.start()); 1; it.next()) {
                        assert(!it.finished());
                        if (*it == thr) {
                            it.remove();
                            break;
                        }
                    }
                    logmsg(loglevel::info, "control interface reaped client thread");
                    if (threads.empty() && localshutdown->ready())
/**/                    goto out;
                } else {
                    if (pfds[i].revents & POLLERR)
                        error::disconnected.fatal("on control listen socket");
                    assert(sock.polled(pfds[i]));
                    auto newfd(sock.accept());
                    if (newfd.isfailure())
                        newfd.failure().fatal("accept on control interface");
                    logmsg(loglevel::info, "control interface accepting new client");
                    auto tr(clientthread::spawn(this,
                                                newfd.success(),
                                                localshutdown));
                    if (tr.isfailure()) {
                        error::from_errno().warn("Cannot build thread for new client");
                        newfd.success().close();
                    } else {
                        threads.pushhead(tr.success());
                    }
                }
                pfds[i].revents = 0;
                r--;
            }
        }
    }
out:
    assert(threads.empty());
    assert(localshutdown->ready());
    assert(dying->empty());
    return;
}

void
controlserver::end()
{
    if (t) {
        assert(localshutdown);
        localshutdown->set(true);
        t->join();
        t = NULL;
    }
    sock.close();
}

maybe<error>
controlserver::setup()
{
    error err;

    {
        auto ls(waitbox<bool>::build());
        if (ls.isfailure()) {
            err = ls.failure();
            goto failed;
        }
        localshutdown = ls.success();
    }

    {
        auto td(waitqueue<clientthread *>::build());
        if (td.isfailure()) {
            err = td.failure();
            goto failed;
        }
        dying = td.success();
    }

    {
        auto sl(unixsocket::listen("mastersock"));
        if (sl.isfailure()) {
            err = sl.failure();
            goto failed;
        }
        sock = sl.success();
    }

    {
        auto ts(thread::spawn(this, &t));
        if (ts.isjust()) {
            err = ts.just();
            goto failed;
        }
    }
    return Nothing;

failed:
    end();
    return err;
}

}

orerror<controlserver *>
controlserver::build(waitbox<shutdowncode> *s)
{
    auto r = new cf::controlserver(s);
    auto e = r->setup();
    if (e.isnothing())
        return (controlserver *)r;
    else
        return e.just();
}

void
controlserver::destroy()
{
    cf::rc(this)->end();
    delete cf::rc(this);
}

controlserver::iface
controlserver::registeriface(wireproto::msgtag t, controliface &interface)
{
    return iface::__mk_iface__(cf::rc(this)->registeriface(t, interface));
}

template class list<cf::controlserver::clientthread *>;
template class waitqueue<cf::controlserver::clientthread *>;
template class list<cf::registration *>;
