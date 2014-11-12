#include "walltime.H"

#include <sys/time.h>
#include <time.h>

#include "parsers.H"
#include "test.H"
#include "timedelta.H"

#include "parsers.tmpl"

walltime::walltime(quickcheck q)
    : v(q) {}

walltime
walltime::now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return walltime(tv.tv_sec * 1000000000l + tv.tv_usec * 1000); }

bool
walltime::operator==(walltime o) const {
    return v == o.v; }

const fields::field &
fields::mk(walltime w) {
    time_t t(w.v / 1000000000l);
    struct tm v_tm;
    if (w.v < 0 && w.v % 1000000000 != 0) t--;
    gmtime_r(&t, &v_tm);
    char time[128];
    auto r = strftime(time,
                      sizeof(time),
                      "%F %T.",
                      &v_tm);
    assert(r > 0);
    assert(r < sizeof(time) - 20);
    long nsec = w.v % 1000000000l;
    if (nsec < 0) nsec += 1000000000;
    sprintf(time + r, "%09ld", nsec);
    return fields::mk(time); }

class walltimeparser : public parser<walltime> {
public: orerror<result> parse(const char *) const;
};
orerror<walltimeparser::result>
walltimeparser::parse(const char *start) const {
    struct tm res_tm;
    memset(&res_tm, 0, sizeof(res_tm));
    char *suffix = strptime(start, "%F %T.", &res_tm);
    if (suffix == NULL) return error::noparse;
    for (int i = 0; i < 9; i++) {
        if (suffix[i] < '0' || suffix[i] > '9') return error::noparse; }
    long nanoseconds = 0;
    for (int i = 0; i < 9; i++) {
        nanoseconds = nanoseconds * 10 + suffix[i] - '0'; }
    return result(walltime((long)timegm(&res_tm) * 1000000000 +
                           nanoseconds),
                  suffix + 9); }

const parser<walltime> &
parsers::_walltime() {
    return *new walltimeparser(); }

void
tests::_walltime() {
    testcaseIO("walltime", "granularity", [] (clientio io) {
            auto last(walltime::now());
            for (int i = 0; i < 100; i++) {
                (timestamp::now() + timedelta::microseconds(2)).sleep(io);
                auto n(walltime::now());
                assert(!(n == last));
                last = n; } });
    testcaseV("walltime", "parser", [] {
            parsers::roundtrip(parsers::_walltime()); });
    testcaseV("walltime", "fixedfields", [] {
            assert(!strcmp(fields::mk(walltime(1408854316000123987l)).c_str(),
                           "2014-08-24 04:25:16.000123987"));
            assert(!strcmp(fields::mk(walltime(1208854316000000000l)).c_str(),
                           "2008-04-22 08:51:56.000000000"));
            assert(!strcmp(fields::mk(walltime(2208854316999999999l)).c_str(),
                           "2039-12-30 10:38:36.999999999"));
            assert(!strcmp(fields::mk(walltime(0l)).c_str(),
                           "1970-01-01 00:00:00.000000000"));
            assert(!strcmp(fields::mk(walltime(-1l)).c_str(),
                           "1969-12-31 23:59:59.999999999"));
            assert(!strcmp(fields::mk(walltime(-1000000000l)).c_str(),
                           "1969-12-31 23:59:59.000000000"));
            assert(!strcmp(fields::mk(walltime(-10000000000000000l)).c_str(),
                           "1969-09-07 06:13:20.000000000")); }); }
