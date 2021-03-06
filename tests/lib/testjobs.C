/* Noddy little job, for testing. */
#include "jobapi.H"
#include "logging.H"
#include "timedelta.H"

#include "maybe.tmpl"

SETVERSION;

jobfunction testfunction;
jobresult
testfunction(jobapi &, clientio) {
    logmsg(loglevel::info, "We are alive!");
    return jobresult::success(); }

jobfunction waitforever;
jobresult
waitforever(jobapi &, clientio io) {
    while (true) (100_s).future().sleep(io); }

jobfunction waitonesecond;
jobresult
waitonesecond(jobapi &, clientio io) {
    (1_s).future().sleep(io);
    return jobresult::success(); }

jobfunction helloworld;
jobresult
helloworld(jobapi &api, clientio io) {
    api.output(streamname::mk("output").fatal("output name"))
        .just()
        ->append(io, buffer("Hello world"));
    return jobresult::success(); }

jobfunction helloinput;
jobresult
helloinput(jobapi &api, clientio io) {
    auto b(api.input(streamname::mk("input").fatal("input name"))
           .just()
           ->read(io));
    if (b.contenteq(buffer("Hello world"))) return jobresult::success();
    else return jobresult::failure(); }

jobfunction echo;
jobresult
echo(jobapi &api, clientio io) {
    api.output(streamname::mk("output").fatal("output name"))
        .just()
        ->append(io, buffer(api.immediate().get("val")
                            .fatal("getting echo val")
                            .c_str()));
    return jobresult::success(); }
