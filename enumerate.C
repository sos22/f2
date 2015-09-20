#include "enumerate.H"

#include <assert.h>

#include "logging.H"

#include "list.tmpl"

template <> unsigned
enumerator::range(unsigned low, unsigned high) {
    if (low == high) return low;
    if (schedule.empty()) {
        for (unsigned x = low + 1; x <= high; x++) {
            pendingpaths.pushtail(currentpath);
            pendingpaths.peektail().pushtail(x); }
        currentpath.pushtail(low);
        return low; }
    else {
        auto r(schedule.pophead());
        assert(r >= low);
        assert(r <= high);
        currentpath.pushtail(r);
        return r; } }
