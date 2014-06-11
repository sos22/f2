#ifndef TIMESTAMP_H__
#define TIMESTAMP_H__

class timedelta;

class timestamp {
    unsigned long v; /* nanoseconds since epoch */
    timestamp(unsigned long _v) : v(_v) {}
public:
    timedelta operator-(timestamp) const;
    timestamp operator+(timedelta) const;
    static timestamp now();
};

#endif /* TIMESTAMP_H__ */
