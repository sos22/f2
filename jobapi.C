#include "jobapi.H"
#include "jobapiimpl.H"

#include "util.H"

class jobapi::impl {
public: jobapi api;
public: impl() : api() {} };

jobapi::impl &
jobapi::implementation() { return *containerof(this, impl, api); }

jobapi::jobapi() {}

jobapi::~jobapi() {}

maybe<nnp<jobapi::outputstream> >
jobapi::output(const streamname &) { return Nothing; }

maybe<nnp<jobapi::inputstream> >
jobapi::input(const streamname &) { return Nothing; }

jobapi &
newjobapi() { return (new jobapi::impl())->api; }

void
deletejobapi(jobapi &api) { delete containerof(&api, jobapi::impl, api); }
