/* A thing for representing ratios.  Not really very specific to
 * percentages, except for some syntactic stuff. */
#ifndef PERCENTAGE_H__
#define PERCENTAGE_H__

class deserialise1;
class serialise1;
namespace fields { class field; }
template <typename> class parser;

class percentage {
private: percentage() = delete;
private: long double val; /* scaled so that 100% is 1 and 0% 0. */
public:  explicit percentage(long double _val) : val(_val / 100) {}
public:  percentage(deserialise1 &);
public:  void serialise(serialise1 &) const;
public:  bool operator==(percentage o) const { return val == o.val; }
public:  bool operator!=(percentage o) const { return val != o.val; }
public:  const fields::field &field() const;
public:  static const ::parser<percentage> &parser(); };

static inline percentage
operator "" _pc(long double x) { return percentage(x); }
static inline percentage
operator "" _pc(unsigned long long x) { return percentage(x); }

namespace fields {
static inline const field &
mk(percentage p) { return p.field(); } }

#endif /* !PERCENTAGE_H__ */
