#ifndef EITHER_H__
#define EITHER_H__

template <typename a, typename b>
class either {
private: bool isleft_;
private: char content[sizeof(a) > sizeof(b) ? sizeof(a) : sizeof(b)];
public:  either(const a &);
public:  either(const b &);
public:  bool isleft() const;
public:  bool isright() const;
public:  const a &left() const;
public:  a &left();
public:  const b &right() const;
public:  b &right();
public:  ~either();
};

#endif /* !EITHER_H__ */
