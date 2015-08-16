/* Noddy little job, for testing. */
#include "jobapi.H"
#include "logging.H"

SETVERSION;

jobfunction testfunction;
jobresult
testfunction(jobapi &, clientio) {
    logmsg(loglevel::info, "We are alive!");
    return jobresult::success(); }

jobfunction waitforever;
jobresult
waitforever(jobapi &api, clientio io) {
    api.shutdown().get(io);
    return jobresult::success(); }
