#include "logging.H"

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include "fields.H"
#include "test.H"
#include "map.H"

#include "list.tmpl"
#include "map.tmpl"
#include "test.tmpl"

const loglevel
loglevel::emergency(5);
const loglevel
loglevel::error(6);
const loglevel
loglevel::notice(7);
const loglevel
loglevel::failure(8);
const loglevel
loglevel::info(9);
const loglevel
loglevel::debug(10);
const loglevel
loglevel::verbose(11);

/* loglevel interface says >= means more serious, but internally
   levels are in descending order. */
bool
loglevel::operator >=(const loglevel &o) const { return level <= o.level; }

bool
loglevel::operator >(const loglevel &o) const { return level < o.level; }

bool
loglevel::operator ==(const loglevel &o) const { return level == o.level; }

const fields::field &
loglevel::field() const {
    if (*this == loglevel::emergency) return fields::mk("emergency");
    else if (*this == loglevel::error) return fields::mk("error");
    else if (*this == loglevel::notice) return fields::mk("notice");
    else if (*this == loglevel::failure) return fields::mk("failure");
    else if (*this == loglevel::info) return fields::mk("info");
    else if (*this == loglevel::debug) return fields::mk("debug");
    else if (*this == loglevel::verbose) return fields::mk("verbose");
    else return "unknown loglevel " + fields::mk(this->level); }

static map<string, logmodule *> &
modules(void) {
    static map<string, logmodule *> _modules;
    return _modules; }

logmodule::logmodule(const string &n)
    : name(n),
      noisy(false) {
    modules().set(n, this); }

class memlogentry {
public: const fields::field &field() const;
public: timestamp when;
public: tid who;
public: const logmodule *module;
public: const char *file;
public: unsigned line;
public: const char *func;
public: loglevel level;
public: char what[]; };

const fields::field &
memlogentry::field() const {
    return padright(when.field(), 24) + " " +
        padright(level.field(), 9) + " " +
        padright(who.field(), 7) +
        (module->name != file
         ? " m:" + padright(module->name.field(), 20)
         : fields::mk("")) +
        " " + padright(fields::mk(file) +
                       ":" + fields::mk(func) +
                       ":" + fields::mk(line).nosep(),
                       40) +
        " " + fields::mk(what).escape(); }

class memlog {
public: list<memlogentry *> log;
public: unsigned count;
public: void append(memlogentry *e) {
    log.append(e);
    if (count == 1000) free(log.pophead());
    else count++; }
public: ~memlog() {
    while (!log.empty()) free(log.pophead()); } };
class ownedmemlog {
public: memlog *inner;
public: ownedmemlog() : inner(new memlog()) {}
public: ~ownedmemlog() { delete inner; inner = NULL; } };
static memlog *threadmemlog() {
    static __thread ownedmemlog i;
    return i.inner; }
static memlog _globalmemlog;
static memlog &globalmemlog(mutex_t::token) {return _globalmemlog;}
static mutex_t globalmemlogmux;
static FILE *logfile;

void
_logmsg(const logmodule &module,
        const char *file,
        unsigned line,
        const char *func,
        loglevel level,
        const fields::field &what) {
    tests::logmsg.trigger(level);
    auto cstr(what.c_str());
    auto len(strlen(cstr));
    auto mle = (memlogentry *)malloc(sizeof(memlogentry) + len + 1);
    mle->when = timestamp::now();
    mle->who = tid::me();
    mle->module = &module;
    mle->file = file;
    mle->line = line;
    mle->func = func;
    mle->level = level;
    memcpy(mle->what, cstr, len + 1);
    const char *mlestr(NULL);
    if (level >= loglevel::error) {
        mlestr = mle->field().c_str();
        syslog(level == loglevel::error ? LOG_CRIT : LOG_ALERT,
               "%s",
               mlestr); }
    if (level >= loglevel::failure || module.noisy) {
        if (mlestr == NULL) mlestr = mle->field().c_str();
        fprintf(logfile, "%s\n", mlestr); }
    if (level >= loglevel::info || module.noisy) {
        if (mlestr == NULL) mlestr = mle->field().c_str();
        fprintf(stderr, "%s\n", mlestr); }
    if (level >= loglevel::debug || module.noisy) {
        globalmemlogmux.locked([mle] (mutex_t::token t) {
                globalmemlog(t).append(mle); }); }
    else if (threadmemlog() != NULL) threadmemlog()->append(mle); }

void
_logmsg(const logmodule &module,
        const char *file,
        unsigned line,
        const char *func,
        loglevel level,
        const char *what) {
    _logmsg(module, file, line, func, level, fields::mk(what)); }

void
dumpmemlog() {
    logmsg(loglevel::notice, "log dump from thread " + tid::me().field());
    maybe<decltype(threadmemlog()->log.start())> titer(Nothing);
    if (threadmemlog() != NULL) titer = threadmemlog()->log.start();
    globalmemlogmux.locked([&titer] (mutex_t::token t) {
            auto giter(globalmemlog(t).log.start());
            if (titer != Nothing) {
                while (!giter.finished() && !titer.just().finished()) {
                    if ((*giter)->when < (*titer.just())->when) {
                        fprintf(stderr, "+++ %s\n", (*giter)->field().c_str());
                        giter.next(); }
                    else {
                        fprintf(stderr,
                                "*** %s\n",
                                (*titer.just())->field().c_str());
                        titer.just().next(); } } }
            while (!giter.finished()) {
                fprintf(stderr, "+++ %s\n", (*giter)->field().c_str());
                giter.next(); } });
    if (titer != Nothing) {
        while (!titer.just().finished()) {
            fprintf(stderr, "*** %s\n", (*titer.just())->field().c_str());
            titer.just().next(); } } }

static void
_logging_exit(int code, void *) {
    if (code != 0) {
        logmsg(loglevel::emergency, "exiting with status " + fields::mk(code));
        dumpmemlog(); } }

void
_initlogging(const char *_ident, list<string> &args) {
    {   auto it(args.start());
        while (!it.finished()) {
            if (*it == "==") break;
            if (*it == "--listmodules") {
                for (auto it2(modules().start()); !it2.finished(); it2.next()) {
                    fprintf(stderr, "%s\n", it2.key().c_str()); }
                exit(0); }
            if (*it == "--verbose") {
                for (auto it2(modules().start()); !it2.finished(); it2.next()) {
                    it2.value()->noisy = true; }
                it.remove();
                continue; }
            auto mod(it->stripprefix("--noisymodule="));
            if (mod != Nothing) {
                it.remove();
                auto m(modules().get(mod.just()));
                if (m == Nothing) {
                    errx(1, "no such module %s", mod.just().c_str()); }
                if (m.just()->noisy) {
                    errx(1, "module %s is already noisy", mod.just().c_str()); }
                m.just()->noisy = true;
                continue; }
            it.next(); } }
    size_t s = strlen(_ident);
    if (s >= 2 && !strcmp(_ident + s - 2, ".C")) s -= 2;
    auto ident = (char *)malloc(s + 5);
    memcpy(ident, _ident, s);
    memcpy(ident + s, ".log", 5);
    logfile = fopen(ident, "a");
    if (logfile == NULL) err(1, "opening %s", ident);
    free(ident);
    on_exit(_logging_exit, NULL); }

namespace tests { event<loglevel> logmsg; }
