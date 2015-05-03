/* Job descriptor type.  This describes a job which could be run on
 * the cluster (as opposed to a task which is currently running). */
#ifndef JOB_H__
#define JOB_H__

#include "filename.H"
#include "list.H"
#include "map.H"
#include "pair.H"
#include "parsers.H"
#include "streamname.H"
#include "string.H"

class deserialise1;
class job;
class jobname;
class serialise1;
namespace fields { class field; }

class job {
public:  typedef pair<jobname, streamname> inputsrc;
public:  filename library;
public:  string function;
public:  map<streamname, inputsrc> inputs;
    /* Private because it must be kept sorted. */
private: list<streamname> outputs;
public:  void serialise(serialise1 &) const;
public:  explicit job(deserialise1 &);
public:  job(
    const filename &_library,
    const string &_function,
    const map<streamname, inputsrc> &_inputs,
    const list<streamname> &_outputs);
public: jobname name() const;
public: bool operator==(const job &) const;
public: bool operator!=(const job &j) const { return !(*this == j); }
public: const fields::field &field() const;
public: static const ::parser<job> &parser(); };

namespace fields {
static inline const field &
mk(const job &j) { return j.field(); } }

#endif /* !JOB_H__ */
