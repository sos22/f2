#ifndef STORAGE_H__
#define STORAGE_H__

#include "jobname.H"
#include "maybe.H"
#include "streamname.H"
#include "streamstatus.H"

class deserialise1;
namespace fields { class field; }
class serialise1;

namespace proto {
namespace storage {
class tag {
private: unsigned v;
private: explicit tag(unsigned);
public:  explicit tag(deserialise1 &);
public:  void serialise(serialise1 &) const;
    /* Inputs: jobname.  Outputs: None */
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
     * maybe<jobname> restartcursor, list<jobname> res. */
public:  static const tag listjobs;
    /* Inputs: jobname, maybe<streamname> start, maybe<unsigned> limit.
     * Outputs: maybe<streamname> restartcursor, list<streamstatus> res. */
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
#endif /* !STORAGE_H__ */
