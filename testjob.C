/* Noddy little job, for testing. */
#include "jobapi.H"
#include "logging.H"

SETVERSION;

jobfunction testfunction;

jobresult
testfunction(waitbox<void> &, clientio) {
    logmsg(loglevel::info, "We are alive!");
    return jobresult::success(); }
