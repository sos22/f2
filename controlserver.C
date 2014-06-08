#include "controlserver.H"

#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include "buffer.H"
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
#include "waitqueue.H"
#include "wireproto.H"

#include "list.tmpl"
#include "waitqueue.tmpl"

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

template class list<thread *>;
template class waitqueue<thread *>;

namespace cf {
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
	    : owner(_owner), fd(_fd), shutdown(_shutdown)
	    {}
	maybe<error> ping(const wireproto::rx_message &, buffer &);
	maybe<error> unrecognised(const wireproto::rx_message &, buffer &);
	maybe<error> getlogs(const wireproto::rx_message &, buffer &);
    public:
	static orerror<clientthread *> spawn(controlserver *,
					     fd_t,
					     waitbox<bool> *);
	thread *thr() { return _thr; }
    };
    waitbox<bool> *localshutdown;
    waitbox<shutdowncode> *globalshutdown;
    mutex_t mux;
    waitqueue<clientthread *> *dying;
    thread *t;
    listenfd sock;

    void run();
    controlserver(waitbox<shutdowncode> *_s)
	: globalshutdown(_s), t(NULL)
	{}

    maybe<error> setup();
    void end();
    ~controlserver()
	{
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
		auto r(outgoing.send(fd));
		if (r.isjust()) {
		    r.just().warn("sending to client");
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
    if (r.isfailure())
	return false;
    assert(r.success() != NULL);
    auto tag(r.success()->t);
    logmsg(loglevel::verbose, "run command %d\n", tag.as_int());
    auto process(tag == proto::PING::tag ? &clientthread::ping :
		 tag == proto::GETLOGS::tag ? &clientthread::getlogs :
		 &clientthread::unrecognised);
    auto res((this->*process)(*r.success(), outgoing));
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
controlserver::clientthread::ping(const wireproto::rx_message &msg,
				  buffer &outgoing)
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
controlserver::clientthread::getlogs(const wireproto::rx_message &msg,
				     buffer &outgoing)
{
    logmsg(loglevel::debug, "fetch logs");
    auto start(msg.getparam(proto::GETLOGS::req::startidx).
	       dflt(memlog_idx::min));

    for (int limit = 200; limit > 0; ) {
	list<memlog_entry> results;
	auto complete(getmemlog(start, limit, results));
	wireproto::resp_message m(msg);
	m.addparam(proto::GETLOGS::resp::msgs, results);
	m.addparam(proto::GETLOGS::resp::complete, complete);
	auto r(m.serialise(outgoing));
	results.flush();
	if (r == Nothing /* success */ ||
	    r.just() != error::overflowed /* unrecoverable error */)
	    return r;
	limit /= 2;
	logmsg(loglevel::verbose,
	       "overflow sending %d log messages, trying %d",
	       limit,
	       limit / 2);
    }

    logmsg(loglevel::failure,
	   "can't send even a single log message without overflowing buffer?");
    return error::overflowed;
}

maybe<error>
controlserver::clientthread::unrecognised(const wireproto::rx_message &msg,
					  buffer &outgoing)
{
    logmsg(loglevel::failure, "Received an unrecognised message");
    return error::unrecognisedmessage;
}

void
controlserver::run()
{
    struct pollfd pfds[4];
    list<clientthread *> threads;
    int nrfds;
    pfds[0] = globalshutdown->fd().poll(POLLIN);
    pfds[1] = localshutdown->fd().poll(POLLIN);
    pfds[2] = dying->fd().poll(POLLIN);
    pfds[3] = sock.poll();
    nrfds = 4;
    while (1) {
	int r = poll(pfds, nrfds, -1);
	if (r < 0)
	    error::from_errno().fatal("polling control interface");
	for (unsigned i = 0; r; i++) {
	    assert(i < ARRAY_SIZE(pfds));
	    if (pfds[i].revents) {
		if (globalshutdown->fd().polled(pfds[i])) {
		    logmsg(loglevel::info, "control interface received global shutdown");
		    assert(!(pfds[i].revents & POLLERR));
		    assert(globalshutdown->ready());
		    localshutdown->set(true);
		    memmove(pfds+i, pfds+i+1, sizeof(pfds[0]) * (nrfds-i-1));
		    nrfds--;
		} else if (localshutdown->fd().polled(pfds[i])) {
		    logmsg(loglevel::info, "control interface received local shutdown");
		    assert(!(pfds[i].revents & POLLERR));
		    memmove(pfds+i, pfds+i+1, sizeof(pfds[0]) * (nrfds-i-1));
		    nrfds--;
		    if (threads.empty())
/**/			goto out;
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
			goto out;
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
    if (dying) {
	dying->destroy();
	dying = NULL;
    }
    if (localshutdown) {
	localshutdown->destroy();
	localshutdown = NULL;
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
