/* Wrapper program which runs a single job in isolation. */
#include <dlfcn.h>
#include <err.h>

#include "clientio.H"
#include "clustername.H"
#include "connpool.H"
#include "filesystemclient.H"
#include "pubsub.H"
#include "job.H"
#include "jobapi.H"
#include "jobapiimpl.H"
#include "jobresult.H"
#include "storageclient.H"

#include "either.tmpl"
#include "parsers.tmpl"

static orerror<jobresult>
runjob(clientio io, const job &j) {
    orerror<jobresult> res(error::unknown);
    char *fname;
    if (asprintf(&fname,
                 "_Z%zd%sR6jobapi8clientio",
                 strlen(j.function.c_str()),
                 j.function.c_str()) < 0) {
        fname = NULL; }
    void *lib = ::dlopen(j.library.str().c_str(), RTLD_NOW|RTLD_LOCAL);
    if (lib == NULL) res = error::dlopen;
    auto v = static_cast<version *>(lib == NULL
                                    ? NULL
                                    : ::dlsym(lib, "f2version"));
    auto f = reinterpret_cast<jobfunction *>(lib == NULL || fname == NULL
                                             ? NULL
                                             : ::dlsym(lib, fname));
    if (f == NULL || v == NULL || *v != version::current) res = error::dlopen;
    else {
        auto &api(newjobapi());
        res = f(api, io);
        deletejobapi(api); }
    if (lib != NULL) ::dlclose(lib);
    free(fname);
    return res; }

static int
runjob(clientio io,
       const clustername &cn,
       const agentname &fsn,
       const job &j) {
    auto cp(connpool::build(cn));
    if (cp.isfailure()) {
        cp.failure().warn("building connpool");
        return 1; }
    auto &fs(filesystemclient::connect(cp.success(), fsn));
    auto storageagents(fs.findjob(io, j.name()));
    fs.destroy();
    if (storageagents.issuccess() && storageagents.success().length() == 0) {
        storageagents = error::toosoon; }
    if (storageagents.isfailure()) {
        cp.success()->destroy();
        storageagents.failure().warn("getting storage agent for job");
        return 1; }
    auto &sc(storageclient::connect(
                 cp.success(), storageagents.success().pophead()));
    auto r(runjob(io, j));
    if (r.issuccess() && r.success().issuccess()) {
        list<nnp<storageclient::asyncfinish> > pending;
        for (auto it(j.outputs().start()); !it.finished(); it.next()) {
            pending.append(sc.finish(j.name(), *it)); }
        orerror<void> failure(Success);
        while (failure == Success && !pending.empty()) {
            failure = pending.pophead()->pop(io); }
        while (!pending.empty()) pending.pophead()->abort();
        if (failure.isfailure()) r = failure.failure(); }
    sc.destroy();
    cp.success()->destroy();
    return r.issuccess(); }

int
main(int argc, char *argv[]) {
    if (argc != 5) {
        errx(
            1,
            "need four arguments: logging name, cluster name, "
            "FS agent name, and job"); }
    initlogging(argv[1]);
    auto cn(parsers::__clustername()
            .match(argv[2])
            .fatal("parsing cluster name " + fields::mk(argv[2])));
    auto fsn(parsers::_agentname()
             .match(argv[3])
             .fatal("parsing agent name " + fields::mk(argv[3])));
    auto j(job::parser()
           .match(argv[4])
           .fatal("parsing job " + fields::mk(argv[4])));
    initpubsub();
    auto r = runjob(clientio::CLIENTIO, cn, fsn, j);
    deinitpubsub(clientio::CLIENTIO);
    deinitlogging();
    return r; }