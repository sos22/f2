#ifndef JOBRESULT_H__
#define JOBRESULT_H__

class deserialise1;
namespace fields { class field; }
class serialise1;

class jobresult {
private: bool succeeded;
private: explicit jobresult(bool _succeeded);
public:  explicit jobresult(deserialise1 &);
public:  void serialise(serialise1 &) const;
public:  static jobresult success();
public:  static jobresult failure();
public:  const fields::field &field() const; };

#endif /* !JOBRESULT_H__ */
