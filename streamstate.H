#ifndef STREAMSTATE_H__
#define STREAMSTATE_H__

class streamstate;

template <typename> class parser;
namespace fields {
class field;
const field &mk(const streamstate &);
};

class streamstate {
public:  static const streamstate empty;
public:  static const streamstate partial;
public:  static const streamstate complete;
private: int fl;
private: explicit streamstate(int);
public:  bool isempty() const;
public:  bool ispartial() const;
public:  bool iscomplete() const;
};

namespace parsers {
const parser< ::streamstate> &streamstate();
}

#endif /* !STREAMSTATE_H__ */
