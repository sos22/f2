#ifndef COORDINATOR_H__
#define COORDINATOR_H__

#include "proto.H"

class serialise1;
class deserialise1;

namespace proto {
namespace coordinator {
class tag : public proto::tag {
private: explicit tag(unsigned char c) : proto::tag(c) {}
public:  explicit tag(deserialise1 &);
    /* Inputs: job
     * Outputs: agentname
     *
     * Makes sure that the job exists on at least one storage agent
     * and then returns one of the agents which has a copy of it. */
public:  static const tag createjob; }; } }

#endif /* !COORINATOR_H__ */
