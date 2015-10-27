/* Timestamps defined against a monotonic, system-local clock.  These
   always advance at once second per second, or as accurately as we
   can get it, and will not be altered if e.g. the user changes the
   system clock.  That makes them useful for most process-local time
   operations but almost meaningless outside of a single process. */
#ifndef TIMESTAMP_H__
#define TIMESTAMP_H__

#include "fields.H"

class clientio;
class quickcheck;
class timedelta;
struct timespec;
class timestamp;
struct timeval;
class walltime;

class timestamp {
    long v; /* nanoseconds since epoch */
    explicit timestamp(long _v) : v(_v) {}
public:
    timestamp(quickcheck);
    timedelta operator-(timestamp) const;
    timestamp operator+(timedelta) const;
    timestamp operator-(timedelta) const;
    bool operator<(timestamp o) const { return v < o.v; }
    bool operator>(timestamp o) const { return v > o.v; }
    bool operator>=(timestamp o) const { return v >= o.v; }
    bool operator==(timestamp o) const { return v == o.v; }
    timespec as_timespec() const;
    walltime aswalltime() const;

    /* Simple wrappers. */
    bool infuture() const { return *this > now(); }
    bool inpast() const { return *this < now(); }

    bool after(timestamp o) const { return *this > o; }
    bool before(timestamp o) const { return *this < o; }

    /* Sleep until the represented time has passed. */
    void sleep(clientio) const;

    static timestamp now();
public: class tsfield : public fields::field {
        friend class timestamp;
    private: long v;
    private: bool isdense;
    private: explicit tsfield(long, bool);
    public:  const tsfield &dense() const;
    public:  const tsfield &undense() const;
    public:  void fmt(fields::fieldbuf &) const; };
public: const tsfield &field() const;
};

#endif /* TIMESTAMP_H__ */
