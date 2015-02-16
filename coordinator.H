#ifndef COORDINATOR_H__
#define COORDINATOR_H__

class serialise1;
class deserialise1;

namespace proto {
namespace coordinator {
class tag {
private: unsigned v;
private: explicit tag(unsigned);
public:  explicit tag(deserialise1 &);
public:  void serialise(serialise1 &) const;
    /* Inputs: jobname.  Outputs: list<slavename> */
public:  static const tag findjob;
    /* Inputs: jobname, streamname.
     * Outputs: list<pair<slavename, streamstatus> > */
public:  static const tag findstream;
    /* Inputs: job
     * Outputs: slavename
     *
     * Makes sure that the job exists on at least one storage slave
     * and then returns one of the slaves which has a copy of it. */
public:  static const tag createjob;
public:  bool operator==(tag o) const { return v == o.v; }
public:  bool operator!=(tag o) const { return v != o.v; } }; } }

#endif /* !COORINATOR_H__ */
