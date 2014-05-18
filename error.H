#ifndef ERROR_H__
#define ERROR_H__

#include "obj.H"

/* Wrapper around errno */
class error : public obj {
    int e;
    explicit error(int _e)
	: e(_e)
	{}
public:
    error() : e(0) {}
    const char *str() const;
    void fatal(const char *msg) const __attribute__((noreturn));
    void warn(const char *msg) const;
    static error from_errno();
    static error from_errno(int);
    static const error disconnected;
    static const error overflowed;
};

#endif
