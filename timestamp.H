/* Timestamps defined against a monotonic, system-local clock.  These
   always advance at once second per second, or as accurately as we
   can get it, and will not be altered if e.g. the user changes the
   system clock.  That makes them useful for most process-local time
   operations but almost meaningless outside of a single process. */
#ifndef TIMESTAMP_H__
#define TIMESTAMP_H__

class timedelta;
struct timespec;
class timestamp;
struct timeval;
class walltime;

namespace fields {
class field;
const field &mk(const timestamp &);
}

class timestamp {
    friend const fields::field &fields::mk(const timestamp &);
    unsigned long v; /* nanoseconds since epoch */
    timestamp(unsigned long _v) : v(_v) {}
public:
    timedelta operator-(timestamp) const;
    timestamp operator+(timedelta) const;
    bool operator<(timestamp o) const { return v < o.v; }
    bool operator>=(timestamp o) const { return v >= o.v; }
    bool operator==(timestamp o) const { return v == o.v; }
    timespec as_timespec() const;
    walltime aswalltime() const;

    /* Sleep until the represented time has passed. */
    void sleep() const;

    static timestamp now();
    static timestamp fromtimespec(timespec);
};

#endif /* TIMESTAMP_H__ */
