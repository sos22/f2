#ifndef LOGGING_H__
#define LOGGING_H__

#include <stdlib.h>
#include <string.h>

#include "controlserver.H"
#include "wireproto.H"

template <typename> class list;
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

class memlog_idx {
    friend class memlog_sink;
    unsigned long val;
    memlog_idx(unsigned long);
    memlog_idx operator++(int);
public:
    WIREPROTO_TYPE(memlog_idx);

    static const memlog_idx min;
    static const memlog_idx max;
    bool operator >=(const memlog_idx &o) const
	{ return val >= o.val; }
    unsigned long as_long() const { return val; }
};

class memlog_entry {
    void operator=(const memlog_entry &) = delete;
public:
    WIREPROTO_TYPE(memlog_entry);
    static maybe<memlog_entry> from_compound(const wireproto::rx_compoundparameter &);

    memlog_idx const idx;
    char *const msg;

    memlog_entry(memlog_idx _idx,
		 const char *_msg)
	: idx(_idx), msg(strdup(_msg))
	{
	}
    memlog_entry()
	: idx(memlog_idx::max), msg(NULL)
	{}
    ~memlog_entry()
	{
	    free(msg);
	}
    memlog_entry(const memlog_entry &o)
	: idx(o.idx), msg(strdup(o.msg))
	{}
};

void initlogging(const char *ident);
void deinitlogging(void);

class getlogsiface : public controliface {
public:
    maybe<error> controlmessage(const wireproto::rx_message &,
				buffer &);
    static getlogsiface singleton;
};

#endif /* !LOGGING_H__ */
