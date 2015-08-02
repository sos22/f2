/* The name of a runnable job.  Produced by digesting the job
 * descriptor. */
#ifndef JOBNAME_H__
#define JOBNAME_H__

#include "digest.H"

class deserialise1;
namespace fields { class field; }
class serialise1;
class job;
class jobname;

template <typename> class parser;
class string;

class jobname {
    friend class job;
private: digest d;
private: explicit jobname(digest _d) : d(_d) {}
public:  string asfilename() const;
public:  bool operator<(const jobname &o) const { return d < o.d; }
public:  bool operator<=(const jobname &o) const { return d <= o.d; }
public:  bool operator==(const jobname &o) const { return d == o.d; }
public:  bool operator!=(const jobname &o) const { return d != o.d; }
public:  bool operator>=(const jobname &o) const { return d >= o.d; }
public:  bool operator>(const jobname &o) const { return d > o.d; }
public:  explicit jobname(deserialise1 &);
public:  void serialise(serialise1 &) const;
public:  const fields::field &field() const;
public:  static const ::parser<jobname> &parser(); };

namespace fields {
static inline const field &
mk(const jobname &jn) { return jn.field(); } }

#endif /* !JOBNAME_H__ */
