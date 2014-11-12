#ifndef STREAMSTATUS_H__
#define STREAMSTATUS_H__

#include "streamname.H"

class streamstatus;

namespace fields {
class field;
const field &mk(const streamstatus &);
}

class streamstatus {
    friend const fields::field &fields::mk(const streamstatus &);
public:  const streamname name;
private: bool finished_;
private: unsigned long size;
private: streamstatus(const streamname &, bool, unsigned long);
public:  static streamstatus partial(const streamname &, unsigned long);
public:  static streamstatus finished(const streamname &, unsigned long);
public:  bool operator<(const streamstatus &sn) const;
public:  bool operator>(const streamstatus &sn) const;
public:  explicit streamstatus(deserialise1 &);
public:  void serialise(serialise1 &) const; };

#endif /* !STREAMSTATUS_H__ */
