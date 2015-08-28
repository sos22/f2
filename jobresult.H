#ifndef JOBRESULT_H__
#define JOBRESULT_H__

#include "steal.H"

class deserialise1;
namespace fields { class field; }
class serialise1;

class jobresult {
private: bool succeeded;
private: explicit jobresult(bool _succeeded);
public:  explicit jobresult(deserialise1 &);
public:  jobresult(_Steal, jobresult &o) : succeeded(o.succeeded) {}
public:  void serialise(serialise1 &) const;
public:  static jobresult success();
public:  static jobresult failure();
public:  bool issuccess() const { return succeeded; }
public:  const fields::field &field() const; };

#endif /* !JOBRESULT_H__ */
