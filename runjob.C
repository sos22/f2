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
#include "main.H"
#include "storageclient.H"

#include "either.tmpl"
#include "list.tmpl"
#include "orerror.tmpl"
#include "parsers.tmpl"

static orerror<jobresult>
runjob(clientio io, storageclient &sc, const job &j) {
    orerror<jobresult> res(error::unknown);
    char *fname;
    if (asprintf(&fname,
                 "_Z%zd%sR6jobapi8clientio",
                 strlen(j.function.c_str()),
                 j.function.c_str()) < 0) {
        fname = NULL; }
    void *lib = ::dlopen(j.library.str().c_str(), RTLD_NOW|RTLD_LOCAL);
    auto v = static_cast<version *>(lib == NULL
                                    ? NULL
                                    : ::dlsym(lib, "f2version"));
    auto f = reinterpret_cast<jobfunction *>(lib == NULL || fname == NULL
                                             ? NULL
                                             : ::dlsym(lib, fname));
    if (f == NULL || v == NULL || *v != version::current) {
        res = error::dlopen;
        logmsg(loglevel::error,
               "cannot open " + j.library.field() +
               ": " + fields::mk(::dlerror())); }
    else {
        auto &api(newjobapi(sc, j));
        res = f(api, io);
        deletejobapi(api); }
    if (lib != NULL) ::dlclose(lib);
    free(fname);
    return res; }

static orerror<jobresult>
runjob(clientio io,
       const clustername &cn,
       const agentname &fsn,
       const job &j) {
    auto cp(connpool::build(cn));
    if (cp.isfailure()) {
        cp.failure().warn("building connpool");
        return cp.failure(); }
    auto &fs(filesystemclient::connect(cp.success(), fsn));
    auto storageagents(fs.findjob(io, j.name()));
    fs.destroy();
    if (storageagents.issuccess() && storageagents.success().length() == 0) {
        storageagents = error::toosoon; }
    if (storageagents.isfailure()) {
        cp.success()->destroy();
        storageagents.failure().warn("getting storage agent for job");
        return storageagents.failure(); }
    auto &sc(storageclient::connect(
                 cp.success(), storageagents.success().pophead()));
    auto r(runjob(io, sc, j));
    if (r.issuccess() && r.success().issuccess()) {
        list<nnp<storageclient::asyncfinish> > pending;
        for (auto it(j.outputs().start()); !it.finished(); it.next()) {
            pending.append(sc.finish(j.name(), *it)); }
        orerror<void> failure(error::unknown);
        failure = Success; /* Don't use the one-stop init because of
                            * spurious compiler warnings. */
        while (failure == Success && !pending.empty()) {
            failure = pending.pophead()->pop(io); }
        while (!pending.empty()) pending.pophead()->abort();
        if (failure != Success) r = failure.failure(); }
    sc.destroy();
    cp.success()->destroy();
    return r; }

orerror<void>
f2main(list<string> &args) {
    if (args.length() != 3 && args.length() != 4) {
        errx(
            1,
            "need three arguments: cluster name, "
            "FS agent name, job, and optionally output FD"); }
    auto cn(parsers::__clustername()
            .match(args.idx(0))
            .fatal("parsing cluster name " + fields::mk(args.idx(0))));
    auto fsn(agentname::parser()
             .match(args.idx(1))
             .fatal("parsing agent name " + fields::mk(args.idx(1))));
    auto j(job::parser()
           .match(args.idx(2))
           .fatal("parsing job " + fields::mk(args.idx(2))));
    fd_t outfd(args.length() == 3
               ? 1
               : (parsers::intparser<unsigned>()
                  .maperr<int>([] (const int &fd) -> orerror<int> {
                          if (fd < 0) return error::noparse;
                          else return fd; })
                  .match(args.idx(3))
                  .fatal("parsing fd " + fields::mk(args.idx(3)))));
    initpubsub();
    auto r = runjob(clientio::CLIENTIO, cn, fsn, j);
    outfd.write(clientio::CLIENTIO, r.field())
        .fatal("reporting result of job (" + r.field() + ")");
    deinitpubsub(clientio::CLIENTIO);
    return Success; }
