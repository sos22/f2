#include <signal.h>

#include "shutdown.H"
#include "controlserver.H"
#include "waitbox.H"

int
main()
{
    signal(SIGPIPE, SIG_IGN);
    auto s(waitbox<shutdowncode>::build());
    if (s.isfailure())
	s.failure().fatal("build shutdown box");
    auto c(controlserver::build(s.success()));
    if (c.isfailure())
	c.failure().fatal("build control interface");
    auto r = s.success()->get();
    c.success()->destroy();
    s.success()->destroy();
    r.finish();
}
