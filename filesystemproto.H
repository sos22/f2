#ifndef FILESYSTEMPROTO_H__
#define FILESYSTEMPROTO_H__

#include "proto.H"

namespace proto {
namespace filesystem {
class tag : public proto::tag {
private: explicit tag(unsigned char c) : proto::tag(c) {}
public:  explicit tag(deserialise1 &);
    
    /* Inputs: jobname
     * Outputs: list<agentname>
     */
public:  static const tag findjob;
    /* Inputs: jobname, streamname.
     * Outputs: list<pair<agentname, streamstatus> > */
public:  static const tag findstream;
    /* Inputs: maybe<jobname>
     * Outputs: maybe<agentname>
     *
     * Nominate a storage agent to store a particular job, or Nothing
     * if no agents can accept new jobs right now.  The choice is
     * arbitrary when multiple agents are available, but will usually
     * be biassed towards an agent which holds the job nominated in
     * the input.
     */
public:  static const tag nominateagent; }; } }

#endif /* !FILESYSTEMPROTO_H__ */
