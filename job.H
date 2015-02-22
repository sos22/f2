/* Job descriptor type.  This describes a job which could be run on
 * the cluster (as opposed to a task which is currently running). */
#ifndef JOB_H__
#define JOB_H__

#include "string.H"

class deserialise1;
class job;
class jobname;
class serialise1;

namespace fields {
class field;
const field &mk(const job &); }

/* Placeholder for now. */
class job {
public: string message;
public: void serialise(serialise1 &) const;
public: explicit job(deserialise1 &);
public: explicit job(const string &);
public: jobname name() const;
public: const fields::field &field() const; };

template <typename> class parser;
namespace parsers {
const parser<job> &_job(); }

#endif /* !JOB_H__ */
