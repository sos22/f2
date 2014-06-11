#include "logging.H"

#include <sys/time.h>
#include <sys/types.h>
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

const memlog_idx
memlog_idx::min(0);

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
        : backlog(10000), outstanding(), next_sequence(1), lock()
        {}
    ~memlog_sink()
        {
            outstanding.flush();
        }
    void msg(const char *s)
        {
            auto t(lock.lock());
            outstanding.pushtail(memlog_entry(next_sequence++, strdup(s)));
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
            assert(f);
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

static list<log_sink *> sinks[7];

const loglevel loglevel::emergency(5);
const loglevel loglevel::error(6);
const loglevel loglevel::notice(7);
const loglevel loglevel::failure(8);
const loglevel loglevel::info(9);
const loglevel loglevel::debug(10);
const loglevel loglevel::verbose(11);

list<log_sink *> &__level_to_sink(loglevel l)
{
    assert(l.level >= loglevel::emergency.level);
    assert(l.level <= loglevel::verbose.level);
    return sinks[l.level-loglevel::emergency.level];
}

static pid_t gettid(void)
{
    return syscall(SYS_gettid);
}

void logmsg(loglevel level, const fields::field &fld)
{
    list<log_sink *> &sink(__level_to_sink(level));
    if (sink.empty())
        return;

    struct timeval now;
    gettimeofday(&now, NULL);

    fields::fieldbuf buf;
    (fields::mk(now).asdate() +
     " pid=" + fields::mk(getpid()).nosep() +
     " tid=" + fields::mk(gettid()).nosep() +
     fields::mk(" ") +
     fld).fmt(buf);
    const char *res(buf.c_str());
    for (auto it(sink.start()); !it.finished(); it.next())
        (*it)->msg(res);

    fields::flush();
}

namespace logsinks {
    memlog_sink *memlog;
    syslog_sink *syslog;
    filelog_sink *filelog;
    stdio_sink *stdiolog;
};

void initlogging(const char *ident)
{
    assert(!logsinks::memlog);
    logsinks::memlog = new memlog_sink();
    logsinks::syslog = new syslog_sink(LOG_INFO);
    logsinks::syslog->open(ident, LOG_DAEMON);
    logsinks::filelog = new filelog_sink();
    logsinks::stdiolog = new stdio_sink(stdout);
    char *logfile;
    int r;
    r = asprintf(&logfile, "%s.log", ident);
    assert(r >= 0);
    auto t(logsinks::filelog->open(logfile));
    free(logfile);
    if (t.isjust())
        t.just().fatal("opening logfile");

    __level_to_sink(loglevel::emergency).pushhead(logsinks::memlog);
    __level_to_sink(loglevel::emergency).pushhead(logsinks::syslog);
    __level_to_sink(loglevel::emergency).pushhead(logsinks::filelog);
    __level_to_sink(loglevel::emergency).pushhead(logsinks::stdiolog);
    __level_to_sink(loglevel::emergency).sanitycheck();

    __level_to_sink(loglevel::error).pushhead(logsinks::memlog);
    __level_to_sink(loglevel::error).pushhead(logsinks::syslog);
    __level_to_sink(loglevel::error).pushhead(logsinks::filelog);
    __level_to_sink(loglevel::error).pushhead(logsinks::stdiolog);

    __level_to_sink(loglevel::notice).pushhead(logsinks::memlog);
    __level_to_sink(loglevel::notice).pushhead(logsinks::syslog);
    __level_to_sink(loglevel::notice).pushhead(logsinks::filelog);
    __level_to_sink(loglevel::notice).pushhead(logsinks::stdiolog);

    __level_to_sink(loglevel::failure).pushhead(logsinks::memlog);
    __level_to_sink(loglevel::failure).pushhead(logsinks::syslog);
    __level_to_sink(loglevel::failure).pushhead(logsinks::stdiolog);

    __level_to_sink(loglevel::info).pushhead(logsinks::memlog);
    __level_to_sink(loglevel::info).pushhead(logsinks::filelog);
    __level_to_sink(loglevel::info).pushhead(logsinks::stdiolog);

    __level_to_sink(loglevel::debug).pushhead(logsinks::memlog);
    __level_to_sink(loglevel::debug).pushhead(logsinks::stdiolog);
}

void deinitlogging(void)
{
    assert(logsinks::memlog);
    delete logsinks::memlog;
    logsinks::memlog = NULL;

    logsinks::syslog->close();
    delete logsinks::syslog;
    logsinks::syslog = NULL;

    logsinks::filelog->close();
    delete logsinks::filelog;
    logsinks::filelog = NULL;

    __level_to_sink(loglevel::emergency).flush();
    __level_to_sink(loglevel::error).flush();
    __level_to_sink(loglevel::failure).flush();
    __level_to_sink(loglevel::notice).flush();
    __level_to_sink(loglevel::info).flush();
    __level_to_sink(loglevel::debug).flush();
    __level_to_sink(loglevel::verbose).flush();
}

getlogsiface::getlogsiface()
    : controliface(proto::GETLOGS::tag)
{}
maybe<error>
getlogsiface::controlmessage(const wireproto::rx_message &msg,
                             buffer &outgoing)
{
    auto start(msg.getparam(proto::GETLOGS::req::startidx).
               dflt(memlog_idx::min));
    logmsg(loglevel::debug,
           fields::mk("fetch logs from ") +
           fields::mk(start.as_long()));
    for (int limit = 200; limit > 0; ) {
        list<memlog_entry> results;
        auto resume(logsinks::memlog->fetch(start, limit, results));
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
