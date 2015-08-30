#ifndef STREAMSTATUS_H__
#define STREAMSTATUS_H__

#include "bytecount.H"
#include "streamname.H"

class streamstatus;

namespace fields {
class field;
const field &mk(const streamstatus &);
}

class streamstatus {
    friend const fields::field &fields::mk(const streamstatus &);
private: streamname name_;
private: bool finished_;
private: bytecount size_;
private: streamstatus(const streamname &, bool, bytecount);
public:  static streamstatus empty(const streamname &);
public:  static streamstatus partial(const streamname &, bytecount);
public:  static streamstatus finished(const streamname &, bytecount);
public:  const streamname &name() const { return name_; }
public:  bytecount size() const { return size_; }
public:  bool isfinished() const { return finished_; }
public:  bool isempty() const { return !finished_ && size_ == 0_B; }
public:  bool operator==(const streamstatus &) const;
public:  bool operator!=(const streamstatus &o) const {
    return !(*this == o); }
public:  explicit streamstatus(deserialise1 &);
public:  void serialise(serialise1 &) const; };

#endif /* !STREAMSTATUS_H__ */
