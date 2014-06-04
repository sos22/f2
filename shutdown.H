#ifndef SHUTDOWN_H__
#define SHUTDOWN_H__

/* Silly little wrapper type around the exit codes passed to exit() */
class shutdowncode {
    int code;
public:
    explicit shutdowncode(int _code)
	: code(_code)
	{}
    /* Call exit() with the given code */
    void finish() __attribute__((noreturn));
};

#endif /* !SHUTDOWN_H__ */
