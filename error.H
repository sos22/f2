#ifndef ERROR_H__
#define ERROR_H__

#include "obj.H"

namespace wireproto {
    template <typename t> class parameter;
    class tx_message;
    class rx_message;
};

template <typename t> class maybe;

/* Wrapper around errno */
class error {
    int e;
    explicit error(int _e)
	: e(_e)
	{}
public:
    void addparam(wireproto::parameter<error>, wireproto::tx_message &) const;
    static maybe<error> getparam(wireproto::parameter<error>,
				 const wireproto::rx_message &);

    error() : e(unknown.e) {}
    bool operator==(const error &o) const { return e == o.e; }
    bool operator!=(const error &o) const { return e != o.e; }

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
    static error from_errno(int) __attribute__((const));

    /* Some unknown error */
    static const error unknown;

    /* Some standard errors */
    static const error disconnected; /* unexpectedly disconnected from
				      * peer */
    static const error overflowed; /* overflowed an internal limit */
    static const error underflowed; /* read buffer does not contain
				       enough data to complete
				       request */
    static const error missingparameter; /* RPC request missing
					  * mandatory parameter */
    static const error invalidmessage; /* RPC message is malformed. */
    static const error unrecognisedmessage; /* RPC message tag unrecognised */
};

#endif
