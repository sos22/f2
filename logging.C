#include "logging.H"

#include <sys/time.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <syscall.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "error.H"
#include "fields.H"
#include "maybe.H"
#include "mutex.H"
#include "proto.H"

#include "list.tmpl"

template class list<memlog_entry>;
template class list<log_sink *>;

const loglevel
loglevel::emergency(5);
const loglevel
loglevel::error(6);
const loglevel
loglevel::notice(7);
const loglevel
loglevel::failure(8);
const loglevel
loglevel::info(9);
const loglevel
loglevel::debug(10);
const loglevel
loglevel::verbose(11);

/* Start it with something which changes when truncated to 32 bits so
   as to make truncation bugs obvious. */
const memlog_idx
memlog_idx::min(1ul << 32);

const memlog_idx
memlog_idx::max(ULONG_MAX);

memlog_idx::memlog_idx(unsigned long _val)
    : val(_val)
{}

memlog_idx
memlog_idx::operator ++(int)
{
    val++;
    return memlog_idx(val - 1);
}

class log_sink {
public:
    virtual void msg(const char *s) = 0;
    virtual ~log_sink() {};
};

class memlog_sink : public log_sink {
    const int backlog;
    list<memlog_entry> outstanding;
    memlog_idx next_sequence;
    mutex_t lock;
public:
    memlog_sink()
        : backlog(10000), outstanding(), next_sequence(memlog_idx::min), lock()
        {}
    ~memlog_sink()
        {
            outstanding.flush();
        }
    void msg(const char *s)
        {
            auto t(lock.lock());
            outstanding.pushtail(memlog_entry(next_sequence++, s));
            while (outstanding.length() > backlog)
                outstanding.pophead();
            lock.unlock(&t);
        }
    maybe<memlog_idx> fetch(memlog_idx start,
                            int limit,
                            list<memlog_entry> &out)
        {
            assert(out.empty());
            auto t(lock.lock());
            int nr;
            nr = 0;
            for (auto it(outstanding.start());
                 !it.finished();
                 it.next()) {
                if (it->idx >= start) {
                    if (nr == limit) {
                        auto res(it->idx);
                        lock.unlock(&t);
                        return res;
                    }
                    nr++;
                    out.pushtail(*it);
                }
            }
            lock.unlock(&t);
            return Nothing;
        }
    
public: void flush() {
    while (outstanding.length() > backlog) outstanding.pophead(); }
    
};

class syslog_sink : public log_sink {
    int priority;
public:
    syslog_sink(int _priority)
        : priority(_priority)
        {}
    void msg(const char *s)
        {
            syslog(priority, "%s", s);
        }
    void open(const char *ident, int facility)
        {
            openlog(ident, 0, facility);
        }
    void close()
        {
            closelog();
        }
};

class filelog_sink : public log_sink {
    FILE *f;
    filelog_sink(const filelog_sink &) = delete;
    void operator=(const filelog_sink &) = delete;
public:
    filelog_sink()
        : f(NULL)
        {}
    ~filelog_sink() { assert(f == NULL); }
    maybe<error> open(const char *filename)
        {
            assert(!f);
            f = fopen(filename, "w");
            if (f)
                return Nothing;
            else
                return error::from_errno();
        }
    void close()
        {
            if (f)
                fclose(f);
            f = NULL;
        }
    void msg(const char *s)
        {
            assert(f);
            fprintf(f, "%s\n", s);
            fflush(f);
        }
};

class stdio_sink : public log_sink {
    FILE *f;
    stdio_sink(const stdio_sink &) = delete;
    void operator=(const stdio_sink &) = delete;
public:
    stdio_sink(FILE *_f)
        : f(_f)
        {}
    void msg(const char *s)
        {
            assert(f);
            fprintf(f, "%s\n", s);
            fflush(f);
        }
};

class logpolicy {
    friend class getlogsiface;
    memlog_sink memlog;
    syslog_sink syslog;
    filelog_sink filelog;
    stdio_sink stdiolog;
    list<log_sink *> sinks[7];
    list<log_sink *> &level_to_sink(loglevel l);
public:
    logpolicy();
    void init(const char *);
    void logmsg(loglevel level, const fields::field &);
    void deinit();
    ~logpolicy() { deinit(); }
};

static logpolicy
policy;

list<log_sink *> &
logpolicy::level_to_sink(loglevel l)
{
    assert(l.level >= loglevel::emergency.level);
    assert(l.level <= loglevel::verbose.level);
    return sinks[l.level-loglevel::emergency.level];
}

logpolicy::logpolicy()
    : memlog(),
      syslog(LOG_INFO),
      filelog(),
      stdiolog(stdout)
{
}

void
logpolicy::init(const char *ident)
{
    syslog.open(ident, LOG_DAEMON);
    char *logfile;
    int r;
    r = asprintf(&logfile, "%s.log", ident);
    assert(r >= 0);
    auto t(filelog.open(logfile));
    if (t.isjust())
        t.just().fatal(fields::mk("opening logfile ") + logfile);
    free(logfile);

    for (auto it(loglevel::begin()); !it.finished(); it.next()) {
        auto &l(level_to_sink(*it));
        l.pushhead(&memlog);
        l.pushhead(&stdiolog);
        if (*it >= loglevel::info)
            l.pushhead(&filelog);
        if (*it >= loglevel::notice)
            l.pushhead(&syslog);
    }
}
void
initlogging(const char *ident)
{
    policy.init(ident);
}

void
logpolicy::logmsg(loglevel level, const fields::field &fld)
{
    auto &sink(level_to_sink(level));
    if (sink.empty())
        return;

    struct timeval now;
    gettimeofday(&now, NULL);

    fields::fieldbuf buf;
    (fields::mk(now).asdate() +
     " pid=" + fields::mk(getpid()).nosep() +
     " tid=" + fields::mk(tid::me()) +
     " level=" + fields::padright(fields::mk(level), 7) +
     " " + fld).fmt(buf);
    const char *res(buf.c_str());
    for (auto it(sink.start()); !it.finished(); it.next())
        (*it)->msg(res);
}
void
logmsg(loglevel level, const fields::field &fld)
{
    policy.logmsg(level, fld);
}

void
logpolicy::deinit(void)
{
    for (auto it(loglevel::begin()); !it.finished(); it.next())
        level_to_sink(*it).flush();
    syslog.close();
    filelog.close();
    memlog.flush();
}
void
deinitlogging(void)
{
    policy.deinit();
}

getlogsiface::getlogsiface()
    : rpcinterface(proto::GETLOGS::tag)
{}
maybe<error>
getlogsiface::message(const wireproto::rx_message &msg,
                      controlconn *,
                      buffer &outgoing)
{
    auto start(msg.getparam(proto::GETLOGS::req::startidx).
               dflt(memlog_idx::min));
    logmsg(loglevel::debug,
           fields::mk("fetch logs from ") +
           fields::mk(start.as_long()));
    for (int limit = 200; limit > 0; ) {
        list<memlog_entry> results;
        auto resume(policy.memlog.fetch(start, limit, results));
        wireproto::resp_message m(msg);
        m.addparam(proto::GETLOGS::resp::msgs, results);
        if (resume.isjust())
            m.addparam(proto::GETLOGS::resp::resume, resume.just());
        auto r(m.serialise(outgoing));
        results.flush();
        if (r == Nothing /* success */ ||
            r.just() != error::overflowed /* unrecoverable error */)
            return r;
        limit /= 2;
        logmsg(loglevel::verbose,
               fields::mk("overflow sending ") +
               fields::mk(limit) +
               fields::mk(" log messages, trying ") +
               fields::mk(limit / 2));
    }

    logmsg(loglevel::failure,
           fields::mk("can't send even a single log message without overflowing buffer?"));
    return error::overflowed;
}
getlogsiface
getlogsiface::singleton;

loglevel::iter
loglevel::begin()
{
    return loglevel::iter();
}

loglevel::iter::iter()
    : cursor(loglevel::emergency.level)
{
}

bool
loglevel::iter::finished() const
{
    return cursor > loglevel::verbose.level;
}

loglevel
loglevel::iter::operator *() const
{
    assert(!finished());
    return loglevel(cursor);
}

void
loglevel::iter::next()
{
    cursor++;
}

bool
loglevel::operator >=(const loglevel &o) const
{
    /* loglevel interface says >= means more serious, but internally
       levels are in descending order. */
    return level <= o.level;
}

bool
loglevel::operator ==(const loglevel &o) const
{
    return level == o.level;
}

const fields::field &
fields::mk(const loglevel &l) {
    if (l == loglevel::emergency)
        return fields::mk("emergency");
    else if (l == loglevel::error)
        return fields::mk("error");
    else if (l == loglevel::notice)
        return fields::mk("notice");
    else if (l == loglevel::failure)
        return fields::mk("failure");
    else if (l == loglevel::info)
        return fields::mk("info");
    else if (l == loglevel::debug)
        return fields::mk("debug");
    else if (l == loglevel::verbose)
        return fields::mk("verbose");
    else
        return "unknown loglevel " + fields::mk(l.level); }

const fields::field &
fields::mk(const memlog_idx &m) {
    return "<memlog_idx:" + fields::mk(m.val) + ">"; }

const fields::field &
fields::mk(const memlog_entry &e) {
    return "<memlog_entry:" + fields::mk(e.idx) +
        "=" + fields::mk(e.msg) + ">"; }

void
logtest(class test &) {
    memlog_sink ms;
    ms.msg("Hello");
    ms.msg("World");
    ms.msg("Goodbye");
    {   list<memlog_entry> l;
        auto truncat(ms.fetch(memlog_idx::min, 0, l));
        assert(l.empty());
        assert(truncat.isjust());
        assert(truncat.just() == memlog_idx::min); }
    {   list<memlog_entry> l;
        auto truncat(ms.fetch(memlog_idx::min, 999, l));
        assert(l.length() == 3);
        assert(truncat == Nothing);
        unsigned long idx = 0;
        for (auto it(l.start()); !it.finished(); it.next()) {
            assert(it->idx.as_long() == idx + memlog_idx::min.as_long());
            switch (idx) {
            case 0:
                assert(!strcmp(it->msg, "Hello"));
                break;
            case 1:
                assert(!strcmp(it->msg, "World"));
                break;
            case 2:
                assert(!strcmp(it->msg, "Goodbye"));
                break;
            default:
                abort();
            }
            idx++;
        }
        l.flush(); }
    {   list<memlog_entry> l;
        auto truncat1(ms.fetch(memlog_idx::min, 1, l));
        assert(l.length() == 1);
        assert(truncat1.isjust());
        assert(l.peekhead().idx == memlog_idx::min);
        assert(!strcmp(l.peekhead().msg, "Hello"));
        l.flush();
        auto truncat2(ms.fetch(truncat1.just(), 1, l));
        assert(l.length() == 1);
        assert(truncat2.isjust());
        assert(l.peekhead().idx == truncat1.just());
        assert(!strcmp(l.peekhead().msg, "World"));
        l.flush();
        auto truncat3(ms.fetch(truncat2.just(), 1, l));
        assert(l.length() == 1);
        assert(truncat3 == Nothing);
        assert(l.peekhead().idx == truncat2.just());
        assert(!strcmp(l.peekhead().msg, "Goodbye"));
        l.flush(); }
    
    ms.flush(); }
