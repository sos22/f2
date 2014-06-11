#include <signal.h>

#include "shutdown.H"
#include "controlserver.H"
#include "fields.H"
#include "logging.H"
#include "waitbox.H"

int
main()
{
    initlogging("master");

    logmsg(loglevel::notice, fields::mk("master starting"));

    signal(SIGPIPE, SIG_IGN);
    auto s(waitbox<shutdowncode>::build());
    if (s.isfailure())
        s.failure().fatal(fields::mk("build shutdown box"));
    auto c(controlserver::build(s.success()));
    if (c.isfailure())
        c.failure().fatal(fields::mk("build control interface"));
    auto r = s.success()->get();
    c.success().destroy();
    s.success()->destroy();
    deinitlogging();
    r.finish();
}
