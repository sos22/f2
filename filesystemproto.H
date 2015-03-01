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
public:  static const tag findjob; }; } }

#endif /* !FILESYSTEMPROTO_H__ */
