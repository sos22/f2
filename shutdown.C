#include "shutdown.H"

#include <stdlib.h>
#include <string.h>

#include "error.H"
#include "fields.H"
#include "util.H"

#include "orerror.tmpl"
#include "waitbox.tmpl"

void
shutdowncode::finish()
{
    exit(code);
}

orerror<shutdowncode>
shutdowncode::parse(const char *what)
{
    if (!strcasecmp(what, "ok"))
        return shutdowncode(0);
    auto r(parselong(what));
    if (r.isfailure()) return r.failure();
    else if (r.success() < 0 || r.success() > 255) return error::noparse;
    else return shutdowncode((int)r.success());
}

const fields::field &
shutdowncode::field() const {
    return "<shutdowncode:" + fields::mk(code) + ">"; }

const shutdowncode
shutdowncode::ok(0);

bool
shutdowncode::operator==(shutdowncode o) const {
    return code == o.code; }
