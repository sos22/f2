#ifndef JOBAPI_H__
#define JOBAPI_H__

#include "clientio.H"
#include "jobresult.H"
#include "version.H"
#include "waitbox.H"

/* The interface which jobs expose to compute agents. */
typedef jobresult jobfunction(waitbox<void> &, clientio);
extern version f2version;

#define SETVERSION version f2version(CURRENTVERSION)

#endif /* !JOBAPI_H__ */
