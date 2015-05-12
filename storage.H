#ifndef STORAGE_H__
#define STORAGE_H__

#include "eq.H"
#include "jobname.H"
#include "list.H"
#include "maybe.H"
#include "proto2.H"
#include "streamname.H"
#include "streamstatus.H"

class deserialise1;
namespace fields { class field; }
class serialise1;

namespace proto {
namespace storage {
class listjobsres {
public: listjobsres(proto::eq::eventid, const list<jobname> &);
public: explicit listjobsres(deserialise1 &);
public: void serialise(serialise1 &) const;
    /* When was the snapshot taken?  It incorporates all events up to
     * and including @when. */
public: proto::eq::eventid when;
    /* The actual snapshot itself. */
public: list<jobname> res; };
class liststreamsres {
public: liststreamsres(proto::eq::eventid,
                       const list<streamstatus> &);
public: explicit liststreamsres(deserialise1 &);
public: void serialise(serialise1 &) const;
    /* When was the snapshot taken?  It incorporates all events up to
     * and including @when. */
public: proto::eq::eventid when;
    /* The actual snapshot itself. */
public: list<streamstatus> res; };
class tag : public proto::tag {
private: explicit tag(unsigned char x) : proto::tag(x) {}
public:  explicit tag(deserialise1 &ds);
    /* Inputs: None.  Outputs: None.  Just check that the service is
     * alive. */
public:  static const tag ping;
    /* Inputs: job.  Outputs: Event ID of the event which will tell
     * the world about the new job. */
public:  static const tag createjob;
    /* Inputs: jobname, streamname, buffer, offset.  Outputs: None */
public:  static const tag append;
    /* Inputs: jobname, streamname.  Outputs: None */
public:  static const tag finish;
    /* Inputs: jobname, streamname, maybe<unsigned long> start,
     * maybe<unsigned long> end.  Outputs: size_t filesize,
     * buffer content. */
public:  static const tag read;
    /* Inputs: None. Outputs: listjobsres.  start in the result is
     * always equal to start in the request.  The result list will
     * contain at most limit entries (if limit is non-Nothing). */
public:  static const tag listjobs;
    /* Inputs: jobname.  Output: job. */
public:  static const tag statjob;
    /* Inputs: jobname, Outputs: liststreamsres. */
public:  static const tag liststreams;
    /* Inputs: jobname, streamname.  Outputs: streamstatus. */
public:  static const tag statstream;
    /* Inputs: jobname.  Outputs: None */
public:  static const tag removejob; };

class event {
public:  enum type {
        t_newjob = 96,
        t_removejob,
        t_finishstream,
    };
public:  void serialise(serialise1 &) const;
public:  explicit event(deserialise1 &);
private: event(type,
               const jobname &,
               const maybe<streamname> &,
               const maybe<streamstatus> &);
public:  type typ;
public:  jobname job;
public:  maybe<streamname> stream; /* For all but jewjob and removejob */
public:  maybe<streamstatus> status; /* for finish stream */
public:  static event newjob(const jobname &);
public:  static event removejob(const jobname &);
public:  static event finishstream(
    const jobname &,
    const streamname &,
    const streamstatus &);
public:  const fields::field &field() const;
};
}
}

namespace fields {
const field &mk(const proto::storage::listjobsres &r);
const field &mk(const proto::storage::liststreamsres &r); }

#endif /* !STORAGE_H__ */
