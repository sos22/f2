#ifndef LOGGING_H__
#define LOGGING_H__

#include <stdlib.h>
#include <string.h>

#include "mutex.H"
#include "wireproto.H"

class loglevel;
class memlog_idx;
class memlog_entry;
namespace fields {
class field;
const field &mk(const loglevel &);
const field &mk(const memlog_idx &);
const field &mk(const memlog_entry &);
};

template <typename> class list;
class log_sink;

class loglevel {
    friend class logpolicy;
    friend const fields::field &fields::mk(const loglevel &);
    friend class iter;
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

    /* x >= y is true if x is equal to or more serious than y */
    bool operator>=(const loglevel &o) const;

    /* simple test for equality */
    bool operator==(const loglevel &o) const;

    /* An easy way of iterating over all defined log levels */
    class iter {
        friend class loglevel;
    private: int cursor;
    private: iter();
    public: bool finished() const;
    public: void next();
    public: loglevel operator*() const;
    };
    static iter begin();
};

void logmsg(loglevel level, const fields::field &);

class memlog_idx {
    friend class memlog_sink;
    friend const fields::field &fields::mk(const memlog_idx &);
    unsigned long val;
    memlog_idx operator++(int);
    WIREPROTO_WRAPPER_TYPE(memlog_idx, unsigned long, private);
public:

    static const memlog_idx min;
    static const memlog_idx max;
    bool operator >=(const memlog_idx &o) const
        { return val >= o.val; }
    bool operator ==(const memlog_idx &o) const {
        return val == o.val; }
    unsigned long as_long() const { return val; }
};

class memlog_entry {
    void operator=(const memlog_entry &) = delete;
    WIREPROTO_TYPE(memlog_entry);
public:

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

class messageresult;
namespace wireproto {
class rx_message;
}
messageresult getlogs(const wireproto::rx_message &);

namespace tests {
template <typename> class event;
extern event< ::loglevel> logmsg;
void logging();
}

#endif /* !LOGGING_H__ */
