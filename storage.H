#ifndef STORAGE_H__
#define STORAGE_H__

class deserialise1;
class serialise1;

namespace proto {
namespace storage {
class tag {
private: unsigned v;
private: explicit tag(unsigned);
public:  explicit tag(deserialise1 &);
public:  void serialise(serialise1 &) const;
    /* Inputs: jobname, streamname.  Outputs: None */
public:  static const tag createempty;
    /* Inputs: jobname, streamname, buffer.  Outputs: None */
public:  static const tag append;
    /* Inputs: jobname, streamname.  Outputs: None */
public:  static const tag finish;
    /* Inputs: jobname, streamname, maybe<unsigned long> start,
     * maybe<unsigned long> end.  Outputs: size_t filesize,
     * buffer content. */
public:  static const tag read;
    /* Inputs: maybe<jobname> start, maybe<unsigned> limit.  Outputs:
     * maybe<jobname> restartcursor, list<jobname> res. */
public:  static const tag listjobs;
    /* Inputs: jobname, maybe<streamname> start, maybe<unsigned> limit.
     * Outputs: maybe<streamname> restartcursor, list<streamname> res. */
public:  static const tag liststreams;
    /* Inputs: jobname, streamname.  Outputs: None. */
public:  static const tag removestream;
public:  bool operator==(tag o) const { return v == o.v; }
public:  bool operator!=(tag o) const { return v != o.v; }
};
}
}
#endif /* !STORAGE_H__ */
