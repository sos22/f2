#ifndef STORAGE_H__
#define STORAGE_H__

#include "eq.H"
#include "jobname.H"
#include "list.H"
#include "maybe.H"
#include "streamname.H"
#include "streamstatus.H"

class deserialise1;
namespace fields { class field; }
class serialise1;

/* XXX there's a lot of code dupe between the stream implementation
 * and the job one.  It might actually have been better to build a
 * more generic filesystem API here. */
namespace proto {
namespace storage {
class listjobsres {
public: listjobsres(proto::eq::eventid,
                    const maybe<jobname> &,
                    const maybe<jobname> &,
                    const list<jobname> &);
public: explicit listjobsres(deserialise1 &);
public: void serialise(serialise1 &) const;
    /* When was the snapshot taken?  It incorporates all events up to
     * and including @when. */
public: proto::eq::eventid when;
    /* The snapshot includes all jobs with names >= start, where
     * Nothing is -ve infinity. */
public: maybe<jobname> start;
    /* It includes all jobs < end, where Nothing is +ve infinity.  end
     * is always greater than start*/
public: maybe<jobname> end;
    /* The actual snapshot itself. */
public: list<jobname> res; };
class liststreamsres {
public: liststreamsres(proto::eq::eventid,
                       const maybe<streamname> &,
                       const maybe<streamname> &,
                       const list<streamstatus> &);
public: explicit liststreamsres(deserialise1 &);
public: void serialise(serialise1 &) const;
    /* When was the snapshot taken?  It incorporates all events up to
     * and including @when. */
public: proto::eq::eventid when;
    /* The snapshot includes all streams with names >= start, where
     * Nothing is -ve infinity. */
public: maybe<streamname> start;
    /* It includes all streams < end, where Nothing is +ve infinity.  end
     * is always greater than start*/
public: maybe<streamname> end;
    /* The actual snapshot itself. */
public: list<streamstatus> res; };
class tag {
private: unsigned v;
private: explicit tag(unsigned);
public:  explicit tag(deserialise1 &);
public:  void serialise(serialise1 &) const;
    /* Inputs: job.  Outputs: None */
public:  static const tag createjob;
    /* Inputs: jobname, streamname.  Outputs: None */
public:  static const tag createstream;
    /* Inputs: jobname, streamname, buffer, offset.  Outputs: None */
public:  static const tag append;
    /* Inputs: jobname, streamname.  Outputs: None */
public:  static const tag finish;
    /* Inputs: jobname, streamname, maybe<unsigned long> start,
     * maybe<unsigned long> end.  Outputs: size_t filesize,
     * buffer content. */
public:  static const tag read;
    /* Inputs: maybe<jobname> start, maybe<unsigned> limit.  Outputs:
     * listjobsres.  start in the result is always equal to start in
     * the request.  The result list will contain at most limit
     * entries (if limit is non-Nothing). */
public:  static const tag listjobs;
    /* Inputs: jobname.  Output: job. */
public:  static const tag statjob;
    /* Inputs: jobname, maybe<streamname> start, maybe<unsigned>
     * limit.  Outputs: liststreamsres.  start in the result is always
     * equal to start in the request.  The result list will contain at
     * most limit entries (if limit is non-Nothing). */
public:  static const tag liststreams;
    /* Inputs: jobname, streamname.  Outputs: streamstatus. */
public:  static const tag statstream;
    /* Inputs: jobname, streamname.  Outputs: None. */
public:  static const tag removestream;
    /* Inputs: jobname.  Outputs: None */
public:  static const tag removejob;
public:  bool operator==(tag o) const { return v == o.v; }
public:  bool operator!=(tag o) const { return v != o.v; }
};

class event {
public:  enum type {
        t_newjob = 96,
        t_removejob,
        t_newstream,
        t_finishstream,
        t_removestream,
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
public:  static event newstream(const jobname &, const streamname &);
public:  static event finishstream(
    const jobname &,
    const streamname &,
    const streamstatus &);
public:  static event removestream(const jobname &, const streamname &);
public:  const fields::field &field() const;
};
}
}

namespace fields {
const field &mk(const proto::storage::listjobsres &r);
const field &mk(const proto::storage::liststreamsres &r); }

#endif /* !STORAGE_H__ */
