#ifndef SHUTDOWN_H__
#define SHUTDOWN_H__

#include "maybe.H"
#include "wireproto.H"

class shutdowncode;

namespace fields {
class field;
const field &mk(const shutdowncode &);
}

namespace spawn {
class process;
}

/* Silly little wrapper type around the exit codes passed to exit() */
class shutdowncode {
    friend const fields::field &fields::mk(const shutdowncode &);
    int code;
    WIREPROTO_WRAPPER_TYPE(shutdowncode, int, public);
public:

    static const shutdowncode ok;

    /* Call exit() with the given code */
    void finish() __attribute__((noreturn));

    static orerror<shutdowncode> parse(const char *);
    bool operator==(shutdowncode) const;
};

#endif /* !SHUTDOWN_H__ */
