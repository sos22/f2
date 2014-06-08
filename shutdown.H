#ifndef SHUTDOWN_H__
#define SHUTDOWN_H__

#include "maybe.H"
#include "wireproto.H"

/* Silly little wrapper type around the exit codes passed to exit() */
class shutdowncode {
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
