/* Names for the various output streams produced by a task.  These are
   strings of printable ASCII characters, excluding the '/'
   character. */
#ifndef STREAMNAME_H__
#define STREAMNAME_H__

#include "string.H"

class streamname;

template <typename> class parser;

namespace fields {
class field;
const field &mk(const streamname &);
}
namespace parsers {
const parser< ::streamname> &_streamname();
}

namespace wireproto {
class tx_message;
}

class streamname {
    friend const fields::field &fields::mk(const streamname &);
    friend const parser< ::streamname> &parsers::_streamname();
    friend class wireproto::tx_message;
private: string content;
private: explicit streamname(const string &);
public:  static maybe<streamname> mk(const string &);
public:  string asfilename() const;
public:  bool operator<(const streamname &o) const;
public:  bool operator>(const streamname &o) const;
};

#endif /* !STREAMNAME_H__ */
