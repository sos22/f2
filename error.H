#ifndef ERROR_H__
#define ERROR_H__

#include "obj.H"

/* Wrapper around errno */
class error {
    int e;
    explicit error(int _e)
	: e(_e)
	{}
public:
    error() : e(0) {}
    /* Return a string representing the error.  The returned string is
     * eternally valid; do not free() or delete it. */
    const char *str() const;
    /* Abort the program with an error */
    void fatal(const char *msg) const __attribute__((noreturn));
    /* Print out a warning based on an error. */
    void warn(const char *msg) const;
    /* Construct an error from the current value of @errno.  This
       clobbers @errno. */
    static error from_errno();
    /* Construct an error from an errno value other than the current
     * one.  @errno is left unmolested. */
    static error from_errno(int);
    /* Error indicating that we were unexpectedly disconnected from a
     * peer. */
    static const error disconnected;
    /* Error indicating that we overflowed one of our internal limits
     * somewhere. */
    static const error overflowed;
};

#endif
