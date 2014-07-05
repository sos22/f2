#ifndef TIMESTAMP_H__
#define TIMESTAMP_H__

class timedelta;
struct timespec;
struct timeval;

class timestamp {
    unsigned long v; /* nanoseconds since epoch */
    timestamp(unsigned long _v) : v(_v) {}
public:
    timedelta operator-(timestamp) const;
    timestamp operator+(timedelta) const;
    bool operator<(const timestamp) const;
    bool operator>=(const timestamp &o) const { return !(*this < o); }
    /* Why do we expose both timespec and timeval versions of this?
       Because timespecs are measured relative to CLOCK_MONOTONIC, and
       are hence process-specific, whereas timevals are measured
       relative to CLOCK_REALTIME, and are hence globally valid. */
    timespec as_timespec() const;
    timeval as_timeval() const;

    /* Sleep until the represented time has passed. */
    void sleep() const;

    static timestamp now();
    static timestamp fromtimeval(timeval);
};

#endif /* TIMESTAMP_H__ */
