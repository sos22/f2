#include "shutdown.H"

#include <stdlib.h>
#include <string.h>

#include "util.H"

#include "waitbox.tmpl"

template class waitbox<shutdowncode>;

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
    if (r.isfailure())
        return r.failure();
    else
        return shutdowncode(r.success());
}
