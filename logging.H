#ifndef LOGGING_H__
#define LOGGING_H__

#include "list.H"

class log_sink;

class loglevel {
    friend list<log_sink *> &__level_to_sink(loglevel l);
    const int level;
    explicit loglevel(int _level)
	: level(_level)
	{}
public:
    /* Something went wrong and we're going to crash. */
    static const loglevel emergency;
    /* Something went wrong and functionality will be reduced. */
    static const loglevel error;
    /* Important status messages */
    static const loglevel notice;
    /* Something went wrong and we're going to try to recover. */
    static const loglevel failure;
    /* Less important status messages */
    static const loglevel info;
    /* Debug messages */
    static const loglevel debug;
    /* Verbose tracing messages */
    static const loglevel verbose;
};

void logmsg(loglevel level,
	    const char *fmt,
	    ...)
    __attribute__((format(printf,2,3)));

void getmemlog(list<const char *> &);

void initlogging(const char *ident);
void deinitlogging(void);

#endif /* !LOGGING_H__ */
