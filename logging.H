#ifndef LOGGING_H__
#define LOGGING_H__

#include "string.H"

namespace fields { class field; };
namespace tests { template <typename> class event; }
template <typename> class list;
class string;

class loglevel {
private: int level;
private: explicit loglevel(int _level) : level(_level) {}
    /* Frequency estimates are all for a healthy system.  Ordinary
     * hardware errors (host crash, network down, etc) can bump things
     * up by one level.  Abnormal hardware errors (checksums, anything
     * else outside of our fault model) bump things up by two
     * levels. */
    /* Something went wrong and we're going to crash.  Should never
     * happen. */
public:  static const loglevel emergency;
    /* Something went wrong and functionality will be reduced.  Should
     * never happen. */
public:  static const loglevel error;
    /* Important status messages. Shouldn't happen more than about
     * once a minute. */
public:  static const loglevel notice;
    /* Something went wrong and we're going to try to recover.
     * Shouldn't happen more than about once a minute. */
public:  static const loglevel failure;
    /* Less important status messages.  Can happen on the order of
     * every few seconds. */
public:  static const loglevel info;
public:  /* Debug messages.  Can happen a couple of times a second. */
public:  static const loglevel debug;
public:  /* Verbose tracing messages.  Can happen at high frequency.  */
public:  static const loglevel verbose;
    
    /* x >= y is true if x is equal to or more serious than y */
public:  bool operator>=(const loglevel &o) const;
public:  bool operator>(const loglevel &o) const;
    
    /* simple test for equality */
public:  bool operator==(const loglevel &o) const;

public:  const fields::field &field() const; };

class logmodule {
public: const string name;
public: bool noisy;
public: explicit logmodule(const string &n); };

static logmodule __logmodule(__BASE_FILE__);

void _logmsg(const logmodule &,
             const char *file,
             unsigned line,
             const char *func,
             loglevel level,
             const fields::field &);
void _logmsg(const logmodule &,
             const char *file,
             unsigned line,
             const char *func,
             loglevel level,
             const char *);
#define logmsg(lvl, ...)                        \
    _logmsg(__logmodule,                        \
            __FILE__,                           \
            __LINE__,                           \
            __func__,                           \
            (lvl),                              \
            __VA_ARGS__)

void dumpmemlog(void);

void _initlogging(const char *ident, list<string> &args);
#define initlogging(args) _initlogging(__BASE_FILE__, args);

namespace tests { extern event<loglevel> logmsg; }

#endif /* !LOGGING_H__ */
