/* The name of a runnable job.  Produced by digesting the job
 * descriptor. */
#ifndef JOBNAME_H__
#define JOBNAME_H__

#include "digest.H"

class deserialise1;
class serialise1;
class jobname;

template <typename> class parser;
class string;

namespace fields {
class field;
const field &mk(const jobname &);
}

namespace parsers {
/* Having the parser called the same thing as the type makes gcc seg
   fault (4.6.3-1ubuntu5, anyway).  Don't do that. */
const parser< ::jobname> &_jobname();
}

class jobname {
    friend const fields::field &fields::mk(const jobname &);
    friend const parser< ::jobname> &parsers::_jobname();
private: digest d;
private: explicit jobname(digest _d) : d(_d) {}
public:  string asfilename() const;
public:  bool operator<(const jobname &o) const;
public:  bool operator>(const jobname &o) const;
public:  explicit jobname(deserialise1 &);
public:  void serialise(serialise1 &) const;
};

#endif /* !JOBNAME_H__ */
