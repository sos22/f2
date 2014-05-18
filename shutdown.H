#ifndef SHUTDOWN_H__
#define SHUTDOWN_H__

class shutdowncode {
    int code;
public:
    explicit shutdowncode(int _code)
	: code(_code)
	{}
    void finish() __attribute__((noreturn));
};

#endif /* !SHUTDOWN_H__ */
