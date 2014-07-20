/* The name of a runnable job.  Produced by digesting the job
 * descriptor. */
#ifndef JOBNAME_H__
#define JOBNAME_H__

#include "digest.H"
#include "wireproto.H"

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
    WIREPROTO_WRAPPER_TYPE(jobname, digest, private);
    friend const fields::field &fields::mk(const jobname &);
    friend const parser< ::jobname> &parsers::_jobname();
private: digest d;
public:  string asfilename() const;
};

#endif /* !JOBNAME_H__ */
