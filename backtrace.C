#include "backtrace.H"

#include <execinfo.h>

#include "fields.H"
#include "util.H"

class backtrace::backtracefield : public fields::field {
public: const class backtrace &trace;
public: explicit backtracefield(const class backtrace &_trace) : trace(_trace){}
public: void fmt(fields::fieldbuf &b) const {
    char **res = backtrace_symbols(trace.entries, trace.nrentries);
    assert(res != NULL);
    b.push("{BACKTRACE ");
    for (unsigned idx = 0; idx < trace.nrentries; idx++) {
        if (idx != 0) b.push("; ");
        b.push(res[idx]); }
    b.push("}");
    free(res); }
public: static fields::field &mk(const backtrace &t) {
    return *new backtracefield(t); } };

backtrace::backtrace() {
    int r = ::backtrace(entries, ARRAYSIZE(entries));
    assert(r >= 0);
    nrentries = r; }

const fields::field &
backtrace::field() const { return backtracefield::mk(*this); }
