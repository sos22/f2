/* Noddy little job, for testing. */
#include "jobapi.H"
#include "logging.H"
#include "timedelta.H"

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

jobfunction waitonesecond;
jobresult
waitonesecond(jobapi &, clientio io) {
    (1_s).future().sleep(io);
    return jobresult::success(); }
