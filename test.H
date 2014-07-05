#ifndef TEST_H__
#define TEST_H__

/* Various bits and bobs of use to the self test bits. */

#include <functional>

namespace tests {

class support {
public:
    void msg(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
    void detail(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
};

void testcaseS(
    const char *component,
    const char *test,
    std::function<void (support &)> doit);
void testcaseV(
    const char *component,
    const char *test,
    std::function<void ()> doit);

void listcomponents();
void listtests(const char *component);
void runtest(const char *component, const char *test);
}

#endif
