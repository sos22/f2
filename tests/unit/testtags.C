#include "test2.H"

#include "test2.tmpl"

/* Dummy test module for the tag types, since they have no code. */
static const testmodule __testtags(
    "tagtypes",
    list<filename>::mk("clientio.H", "empty.H"));
