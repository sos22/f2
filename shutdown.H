#ifndef SHUTDOWN_H__
#define SHUTDOWN_H__

#include "maybe.H"
#include "wireproto.H"

class shutdowncode;

namespace fields {
class field;
const field &mk(const shutdowncode &);
}

/* Silly little wrapper type around the exit codes passed to exit() */
class shutdowncode {
    friend const fields::field &fields::mk(const shutdowncode &);
    int code;
    shutdowncode(int _code)
        : code(_code)
        {}
public:
    WIREPROTO_TYPE(shutdowncode);

    /* Call exit() with the given code */
    void finish() __attribute__((noreturn));

    static orerror<shutdowncode> parse(const char *);
};

#endif /* !SHUTDOWN_H__ */
