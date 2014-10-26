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
public:  void mkright(const b &);
public:  void mkleft(const a &);
public:  ~either();
};

template <typename r, typename a> either <a, r> left(const a &what) {
    return either<a, r>::left(what); }

template <typename l, typename a> either <l, a> right(const a &what) {
    return either<l, a>::right(what); }

/* Specialisation when one of the parameters is void. */
template <typename a>
class either<void, a> {
private: void operator=(const either &) = delete;
private: bool const isleft_;
private: char content[sizeof(a)];
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
