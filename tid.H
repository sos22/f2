#ifndef TID_H__
#define TID_H__

namespace fields { class field; }

class tid {
private: explicit tid(unsigned);
private: unsigned val;
public:  tid() = delete;
public:  static tid me();
public:  static tid nonexistent();
public:  unsigned os() const { return val; }
public:  bool operator==(const tid &) const;
public:  const fields::field &field() const;
};

#endif /* !TID_H__ */
