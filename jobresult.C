#include "jobresult.H"

#include "serialise.H"

jobresult::jobresult(bool s) : succeeded(s) {}

jobresult::jobresult(deserialise1 &ds) : succeeded(ds) {}

void
jobresult::serialise(serialise1 &s) const { s.push(succeeded); }

jobresult
jobresult::success() { return jobresult(true); }

jobresult
jobresult::failure() { return jobresult(false); }
