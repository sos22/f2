#ifndef EITHER_H__
#define EITHER_H__

template <typename a, typename b>
class either {
private: void operator=(const either &) = delete;
private: bool isleft_;
private: char content[sizeof(a) > sizeof(b) ? sizeof(a) : sizeof(b)];
private: either() {};
public:  either(const either &);
public:  static either left(const a &);
public:  either(const b &);
public:  static either right(const b &);
public:  bool isleft() const;
public:  bool isright() const;
public:  const a &left() const;
public:  a &left();
public:  const b &right() const;
public:  b &right();
public:  ~either();
};

/* Specialisation when one of the parameters is void. */
template <typename a>
class either<void, a> {
public: either();
public: either(const a &);
public: bool isleft() const;
public: bool isright() const;
public: const a &right() const;
public: a &right();
public: ~either();
};

namespace tests {
void either();
}

#endif /* !EITHER_H__ */
