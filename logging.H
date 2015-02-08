#ifndef LOGGING_H__
#define LOGGING_H__

#include <stdlib.h>
#include <string.h>

#include "mutex.H"

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
    /* Frequency estimates are all for a healthy system.  Ordinary
     * hardware errors (host crash, network down, etc) can bump things
     * up by one level.  Abnormal hardware errors (checksums, anything
     * else outside of our fault model) bump things up by two
     * levels. */
    /* Something went wrong and we're going to crash.  Should never
     * happen. */
    static const loglevel emergency;
    /* Something went wrong and functionality will be reduced.  Should
     * never happen. */
    static const loglevel error;
    /* Important status messages. Shouldn't happen more than about
     * once a minute. */
    static const loglevel notice;
    /* Something went wrong and we're going to try to recover.
     * Shouldn't happen more than about once a minute. */
    static const loglevel failure;
    /* Less important status messages.  Can happen on the order of
     * every few seconds. */
    static const loglevel info;
    /* Debug messages.  Can happen a couple of times a second. */
    static const loglevel debug;
    /* Verbose tracing messages.  Can happen at high frequency.  */
    static const loglevel verbose;

    /* x >= y is true if x is equal to or more serious than y */
    bool operator>=(const loglevel &o) const;
    bool operator>(const loglevel &o) const;

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
void logmsg(loglevel level, const char *);

class memlog_idx {
    friend class memlog_sink;
    friend const fields::field &fields::mk(const memlog_idx &);
    unsigned long val;
    memlog_idx operator++(int);
private: explicit memlog_idx(unsigned long _val) : val(_val) {}
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

namespace tests {
template <typename> class event;
extern event< ::loglevel> logmsg;
void logging();
}

#endif /* !LOGGING_H__ */
