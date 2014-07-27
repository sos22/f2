#ifndef STREAMSTATUS_H__
#define STREAMSTATUS_H__

#include "streamname.H"
#include "wireproto.H"

class streamstatus;

namespace fields {
class field;
const field &mk(const streamstatus &);
}

class streamstatus {
    friend const fields::field &fields::mk(const streamstatus &);
public:  const streamname name;
private: bool finished_;
private: uint64_t size;
private: streamstatus(const streamname &, bool, uint64_t);
public:  static streamstatus partial(const streamname &, uint64_t);
public:  static streamstatus finished(const streamname &, uint64_t);
public:  bool operator<(const streamstatus &sn) const;
public:  bool operator>(const streamstatus &sn) const;
    WIREPROTO_TYPE(streamstatus);
};

#endif /* !STREAMSTATUS_H__ */
