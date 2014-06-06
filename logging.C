#include "logging.H"

#include <sys/time.h>
#include <sys/types.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "error.H"
#include "maybe.H"
#include "mutex.H"

#include "list.tmpl"

class log_sink {
public:
    virtual void msg(const char *s) = 0;
};

class memlog_sink : public log_sink {
    const int backlog;
    list<char *> outstanding;
    mutex_t lock;
public:
    memlog_sink()
	: backlog(10000)
	{}
    ~memlog_sink()
	{
	    while (!outstanding.empty())
		free(outstanding.pophead());
	}
    void msg(const char *s)
	{
	    auto t(lock.lock());
	    outstanding.pushtail(strdup(s));
	    while (outstanding.length() > backlog)
		free(outstanding.pophead());
	    lock.unlock(&t);
	}
    void fetch(list<const char *> &out)
	{
	    assert(out.empty());
	    auto t(lock.lock());
	    for (auto it(outstanding.start()); !it.finished(); it.next())
		out.pushtail(strdup(*it));
	    lock.unlock(&t);
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

void logmsg(loglevel level, const char *fmt, ...)
{
    list<log_sink *> &sink(__level_to_sink(level));
    if (sink.empty())
	return;

    struct timeval now;
    gettimeofday(&now, NULL);
    struct tm now_tm;
    gmtime_r(&now.tv_sec, &now_tm);

    va_list args;
    char *fmted;
    int r;
    va_start(args, fmt);
    r = vasprintf(&fmted, fmt, args);
    va_end(args);
    assert(r > 0);

    char fmt2[128];
    r = strftime(fmt2,
		 sizeof(fmt2),
		 "%F %T.%%06d pid=%%d tid=%%d %%s",
		 &now_tm);
    assert(r > 0);
    assert(r < (int)sizeof(fmt2));
    char *res;
    r = asprintf(&res, fmt2, now.tv_usec, getpid(), gettid(), fmted);
    assert(r > 0);
    free(fmted);

    for (auto it(sink.start()); !it.finished(); it.next())
	(*it)->msg(res);

    free(res);
}

namespace logsinks {
    memlog_sink *memlog;
    syslog_sink *syslog;
    filelog_sink *filelog;
};

void initlogging(const char *ident)
{
    assert(!logsinks::memlog);
    logsinks::memlog = new memlog_sink();
    logsinks::syslog = new syslog_sink(LOG_INFO);
    logsinks::syslog->open(ident, LOG_DAEMON);
    logsinks::filelog = new filelog_sink();
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
    __level_to_sink(loglevel::emergency).sanitycheck();

    __level_to_sink(loglevel::error).pushhead(logsinks::memlog);
    __level_to_sink(loglevel::error).pushhead(logsinks::syslog);
    __level_to_sink(loglevel::error).pushhead(logsinks::filelog);

    __level_to_sink(loglevel::notice).pushhead(logsinks::memlog);
    __level_to_sink(loglevel::notice).pushhead(logsinks::syslog);
    __level_to_sink(loglevel::notice).pushhead(logsinks::filelog);

    __level_to_sink(loglevel::failure).pushhead(logsinks::memlog);
    __level_to_sink(loglevel::failure).pushhead(logsinks::syslog);

    __level_to_sink(loglevel::info).pushhead(logsinks::memlog);
    __level_to_sink(loglevel::info).pushhead(logsinks::filelog);

    __level_to_sink(loglevel::debug).pushhead(logsinks::memlog);
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

void getmemlog(list<const char *> &out)
{
    assert(out.empty());
    assert(logsinks::memlog);
    logsinks::memlog->fetch(out);
}
