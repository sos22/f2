#ifndef TEST_H__
#define TEST_H__

/* Various bits and bobs of use to the self test bits. */
class test {
public:
    void msg(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
    void detail(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
};

#endif
