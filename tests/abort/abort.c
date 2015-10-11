/* Trivial program which just calls abort(), for testing. */
#include <sys/resource.h>
#include <sys/time.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

int
main() {
    /* Core dumps from this program are just noise, and tend to
     * confuse the harness, so turn them off. */
    struct rlimit rl;
    memset(&rl, 0, sizeof(rl));
    if (setrlimit(RLIMIT_CORE, &rl) < 0) err("setrlimit");
    abort(); }
