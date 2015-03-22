#ifndef JOBAPI_H__
#define JOBAPI_H__

#include "buffer.H"
#include "bytecount.H"
#include "clientio.H"
#include "jobresult.H"
#include "maybe.H"
#include "nnp.H"
#include "streamname.H"
#include "version.H"
#include "waitbox.H"

/* The interface which compute agents expose to jobs. */
/* A final class with virtual methods looks a bit odd.  It's final
 * because nobody should ever derive from it.  The methods are virtual
 * because jobs need to call them through function pointers, rather
 * than relying on the linker, to get the right sort of dlopen()
 * behaviour. */
/* Most of these jobs are failure oblivious.  If, for instance, a
 * storage slave disappears while we're trying to talk to it then the
 * result isn't an API returning an error, but F2 retrying the whole
 * job. */
class jobapi final {
public:  class impl;
    friend class impl;
private: impl &implementation();
private: jobapi();
private: jobapi(const jobapi &) = delete;
private: void operator=(const jobapi &) = delete;
private: ~jobapi();
    /* A waitbox which becomes set if the results of the job are no
     * longer interesting (usually because the compute agent is
     * shutting down). */
public:  virtual waitbox<void> &shutdown();
    /* A wrapper for an output stream, exposing all of the operations
     * which a job can perform against it. */
public:  class outputstream {
        /* Add some bytes to the end of the stream. */
    public: virtual void append(const buffer &); };
    /* ``Open'' an output stream, mapping from a stream name to an
     * actual outputstream which can be used to record the job's
     * outputs.  Returns Nothing if no output stream has that
     * streamname. */
public:  virtual maybe<nnp<outputstream> > output(const streamname &);
    /* A wrapper for an input stream, exposing all of the operations
     * which a job can perform against it. */
public:  class inputstream {
    public: virtual buffer read(bytecount start, bytecount end); };
    /* ``Open'' an input stream, mapping from a stream name to an
     * actual inputstream which can be used to extract the job's
     * indirect input.  Returns Nothing if no input stream has that
     * streamname. */
public:  virtual maybe<nnp<inputstream> > input(const streamname &); };

/* The interface which jobs expose to compute agents. */
typedef jobresult jobfunction(jobapi &, clientio);
extern version f2version;
#define SETVERSION version f2version(CURRENTVERSION)

#endif /* !JOBAPI_H__ */
