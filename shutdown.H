#ifndef SHUTDOWN_H__
#define SHUTDOWN_H__

#include "maybe.H"

class shutdowncode;

namespace fields { class field; }

/* Silly little wrapper type around the exit codes passed to exit() */
class shutdowncode {
    int code;
public: explicit shutdowncode(int _code) : code(_code) {}
public:

    static const shutdowncode ok;

    /* Call exit() with the given code */
    void finish() __attribute__((noreturn));

    static orerror<shutdowncode> parse(const char *);
    bool operator==(shutdowncode) const;
public: const fields::field &field() const; };

#endif /* !SHUTDOWN_H__ */
