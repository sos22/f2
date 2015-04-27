#include "test.H"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <functional>

#include "fields.H"
#include "filename.H"
#include "list.H"
#include "peername.H"
#include "shutdown.H"
#include "util.H"
#include "waitbox.H"

#include "list.tmpl"
#include "test.tmpl"

namespace tests {

#if TESTING
tests::eventwaiter<void>::eventwaiter(
    tests::event<void> &_evt,
    std::function<void ()> _action)
    : eventwaiter<void *>(_evt,
                          [_action] (void *) { _action(); }) {}
#endif

/* Careful: the ``real'' template arguments for the actual instance
 * we're called on don't necessarily match the static ones. */
template <> void
tests::hookpoint<void>::wait() {
    /* This could in theory wait quite a while without a proper
     * clientio token, if someone's specified a test hook which takes
     * a long time (and from a destructor at that!).  It's probably
     * good enough for testing infrastructure. */
    auto token(mux.lock());
    while (refcount != 0) token = idle.wait(clientio::CLIENTIO, &token);
    mux.unlock(&token); }

/* Careful: the ``real'' template arguments for the actual instance
 * we're called on don't necessarily match the static ones. */
template <> void
tests::hookpoint<void>::set(tests::hook<void> *what) {
    if (what == NULL) {
        assert(hooked != Nothing);
        wait();
        /* wait() isn't quite a full memory barrier.  Do an explicit
         * one here, for general sanity. */
        mb();
        hooked = Nothing; }
    else {
        assert(hooked == Nothing);
        mb();
        hooked = what; } }

} /* End namespace tests */
