/* Job which just exec()s an external program. */
#include "jobapi.H"
#include "logging.H"
#include "spawn.H"
#include "thread.H"

#include "map.tmpl"
#include "orerror.tmpl"
#include "thread.tmpl"
#include "waitbox.tmpl"

SETVERSION;

namespace {
class stdinthread : public thread {
public: waitbox<jobresult> &earlyexit;
public: fd_t writeto;
public: jobapi::inputstream &readfrom;
public: stdinthread(constoken t,
                    waitbox<jobresult> &_earlyexit,
                    fd_t _writeto,
                    jobapi::inputstream &_readfrom)
    : thread(t),
      earlyexit(_earlyexit),
      writeto(_writeto),
      readfrom(_readfrom) {}
public: void run(clientio io) {
    writeto.nonblock(true).fatal("nonblock stdin");
    subscriber sub;
    subscription ee(sub, earlyexit.pub());
    iosubscription ios(sub, writeto.poll(POLLOUT));
    auto b(readfrom.read(io, 0_B, 4096_B));
    while (!earlyexit.ready()) {
        logmsg(loglevel::debug,
               fields::mk(b.avail()) + " to send to stdin, from " +
               fields::mk(b.offset()));
        if (b.avail() == 0) {
            logmsg(loglevel::debug, "reached end of stdin");
            break; }
        auto scc = sub.wait(io);
        if (scc == &ee) continue;
        assert(scc == &ios);
        auto f(b.sendfast(writeto));
        ios.rearm();
        if (f == error::disconnected) {
            logmsg(loglevel::debug, "child closed stdin");
            break; }
        if (b.avail() < 2048) {
            logmsg(loglevel::debug,
                   fields::mk(b.avail()) + " avail, read more stdin from " +
                   fields::mk(b.offset()));
            auto b2(readfrom.read(io,
                                  bytecount::bytes(b.offset() + b.avail()),
                                  4096_B));
            b.transfer(b2); } }
    writeto.close(); } };
class stdouterrthread : public thread {
public: waitbox<jobresult> &earlyexit;
public: fd_t readfrom;
public: maybe<nnp<jobapi::outputstream> > writeto;
public: stdouterrthread(constoken t,
                        waitbox<jobresult> &_earlyexit,
                        fd_t _readfrom,
                        const maybe<nnp<jobapi::outputstream> > &_writeto)
    : thread(t),
      earlyexit(_earlyexit),
      readfrom(_readfrom),
      writeto(_writeto) {}
public: void run(clientio io) {
    readfrom.nonblock(true).fatal("nonblock output");
    subscriber sub;
    subscription ee(sub, earlyexit.pub());
    iosubscription ios(sub, readfrom.poll(POLLIN));
    while (!earlyexit.ready()) {
        auto scc = sub.wait(io);
        if (scc == &ee) continue;
        assert(scc == &ios);
        buffer b;
        auto e(b.receivefast(readfrom));
        if (e == error::disconnected) {
            logmsg(loglevel::debug, "job closed std{out,err}");
            break; }
        e.fatal("reading child std{out,err}");
        ios.rearm();
        if (b.empty()) continue;
        if (writeto == Nothing) {
            logmsg(loglevel::notice,
                   "job unexpectedly produced output: " + b.field());
            earlyexit.setif(jobresult::failure());
            break; }
        else writeto.just()->append(io, b); }
    readfrom.close(); } }; }

jobfunction exec;
jobresult
exec(jobapi &api, clientio io) {
    auto args(api.immediate());
    map<unsigned, string> params;
    auto progname(args
                  .get("program")
                  .fatal("program arg missing"));
    unsigned maxarg = 0;
    for (auto it(args.start()); !it.finished(); it.remove()) {
        if (it.key() == "program") continue;
        auto p(("arg" + parsers::intparser<unsigned>())
               .match(it.key())
               .fatal("bad argument " + it.key().field()));
        params.set(p, it.value());
        if (p >= maxarg) maxarg = p;
        it.remove(); }
    if (!args.isempty()) {
        error::invalidparameter.fatal(
            "unrecognised arguments " + args.field()); }
    spawn::program prog((filename(progname)));
    /* Arguments must be dense */
    for (unsigned x = 0; x < maxarg; x++) {
        auto p(params.get(x));
        if (p == Nothing) {
            error::invalidparameter
                .fatal(
                    "argument " + fields::mk(x) + " missing; "
                    "args: " + params.field()); }
        prog.addarg(p.just()); }
    waitbox<jobresult> earlyexit;
    /* stdin is either a pipe which we fill with an input stream, or
     * /dev/null. */
    auto stdinstream(api.input(streamname::mk("stdin").fatal("stdin")));
    maybe<fd_t> stdinread(Nothing);
    stdinthread *stdinthr = nullptr;
    if (stdinstream == Nothing) {
        /* No defined stdin -> /dev/null */
        stdinread = filename("/dev/null").openro().fatal("opening /dev/null");}
    else {
        /* Defined stdin -> it's a pipe which we're going to fill from
         * the stream. */
        auto p(fd_t::pipe().fatal("stdin pipe"));
        stdinread = p.read;
        stdinthr = thread::start<stdinthread>(
            fields::mk("stdin"),
            earlyexit,
            p.write,
            *stdinstream.just()); }
    /* stdout and stderr can be redirected. */
    auto stdoutp(fd_t::pipe().fatal("stdout"));
    auto stderrp(fd_t::pipe().fatal("stdout"));
    auto &stdoutthr(*thread::start<stdouterrthread>(
                        fields::mk("stdout"),
                        earlyexit,
                        stdoutp.read,
                        api.output(streamname::mk("stdout").fatal("stdout"))));
    auto &stderrthr(*thread::start<stdouterrthread>(
                        fields::mk("stderr"),
                        earlyexit,
                        stderrp.read,
                        api.output(streamname::mk("stderr").fatal("stderr"))));
    prog.addfd(stdinread.just(), 0);
    prog.addfd(stdoutp.write, 1);
    prog.addfd(stderrp.write, 2);
    auto &proc(*spawn::process::spawn(prog)
               .fatal("starting " + prog.field()));
    stdinread.just().close();
    stdoutp.write.close();
    stderrp.write.close();
    {   subscriber sub;
        subscription eesub(sub, earlyexit.pub());
        spawn::subscription procsub(sub, proc);
        while (true) {
            sub.wait(io);
            if (earlyexit.ready() || proc.hasdied() != Nothing) break; } }
    auto tok(proc.hasdied());
    if (tok == Nothing) {
        logmsg(loglevel::debug, "kill job because of stdio stuff");
        assert(earlyexit.ready());
        proc.kill(); }
    else {
        auto r(proc.join(tok.fatal("job un-exited?")));
        logmsg(loglevel::debug, "job exited with " + fields::mk(r));
        earlyexit.setif(
            r.isleft() && r.left() == shutdowncode::ok
            ? jobresult::success()
            : jobresult::failure()); }
    assert(earlyexit.ready());
    stderrthr.join(io);
    stdoutthr.join(io);
    if (stdinthr != NULL) stdinthr->join(io);
    return earlyexit.get(io); }
