#include "testassert.H"
#include "testassert.tmpl"

#include "fields.H"

testassert::val<_Nothing>::val(
    const char *, const std::function<_Nothing ()> &) {}

const fields::field &
testassert::val<_Nothing>::field(unsigned, bool, const char *) const {
    return fields::mk("Nothing"); }

_Nothing
testassert::val<_Nothing>::eval() { return Nothing; }
