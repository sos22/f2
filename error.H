#ifndef ERROR_H__
#define ERROR_H__

class error;

namespace fields {
class field;
const field &mk(error); }

class deserialise1;
class serialise1;
template <typename t> class orerror;
class quickcheck;

/* Wrapper around errno.  Must be POD. */
class error {
    friend class errorfield;
    int e;
    explicit error(const int &_e) : e(_e) {}
    int unwrap() const;
public:
    error(deserialise1 &);
    error() : e(unknown.e) {}
    error(_Steal, error o) : e(o.e) {}
    static error __test_mkerr(int _e) { return error(_e); }

    bool operator==(const error &o) const { return e == o.e; }
    bool operator!=(const error &o) const { return e != o.e; }

    /* Abort the program with an error */
    void fatal(const fields::field &) const __attribute__((noreturn));
    void fatal(const char *) const __attribute__((noreturn));

    /* Print out a warning based on an error. */
    void warn(const fields::field &) const;
    void warn(const char *) const;

    void serialise(serialise1 &) const;

    const fields::field &field() const { return fields::mk(*this); }

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
    static const error invalidmessage; /* RPC message is malformed. */
    static const error unrecognisedmessage; /* RPC message tag unrecognised */
    static const error noparse; /* Cannot parse user-provided argument */
    static const error timeout; /* Timed out performing some operation */
    static const error truncated; /* Message too big for underlying transport */
    static const error badversion; /* Unrecognised protocol version requested */
    static const error invalidparameter; /* RPC message parameter is bad */
    static const error already; /* Operation already completed */
    static const error notfound; /* File or dictionary key not found */
    static const error notafile; /* Expected regular file; got something else */
    static const error toolate; /* Operation arrived too late to be processed */
    static const error toosoon; /* Operation arrived too soon to be processed */
    static const error pastend; /* Offset is past end of file */
    static const error signalled; /* Child killed by signal. */
    static const error nothing; /* maybe<> was Nothing when it should
                                 * have been just() */
    static const error notempty; /* Container not empty */
    static const error wouldblock; /* Cannot complete operation
                                    * without blocking. */
    static const error shutdown; /* The thing which is being called is
                                  * shutting down. */
    static const error range; /* Parameter is out of range. */
    static const error badinterface; /* Interface not supported. */
    static const error aborted; /* Call aborted. */
    static const error badqueue; /* Event queue not currently live. */
    static const error eventsdropped; /* Queue dropped one or more events. */
    static const error badsubscription; /* Unknown EQ subscription. */
    static const error notadir; /* Expected directory; got something else */
    static const error eqstatemismatch; /* Event queue state not as expected. */
    static const error nostorageagents; /* No storage slaves available. */
    static const error dlopen; /* Error loading job library. */
    static const error duplicate; /* Duplicate key in map. */
};

namespace __error_private {
extern const int firsterror;
extern const int lasterror;
}

#endif
