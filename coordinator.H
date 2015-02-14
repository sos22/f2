#ifndef COORDINATOR_H__
#define COORDINATOR_H__

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
       Outputs: list<pair<slavename, streamstatus> > */
public:  static const tag findstream;
    
public:  bool operator==(tag o) const { return v == o.v; }
public:  bool operator!=(tag o) const { return v != o.v; } }; } }

#endif /* !COORINATOR_H__ */
