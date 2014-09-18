#include "spark.H"

template <> void
spark<void>::thr::run(clientio) {
    what();
    owner->wb.set(); }
