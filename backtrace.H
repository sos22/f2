#ifndef BACKTRACE_H__
#define BACKTRACE_H__

namespace fields { class field; }

class backtrace {
private: class backtracefield;
private: unsigned nrentries;
private: void *entries[20];
public:  backtrace();
public:  const fields::field &field() const; };

#endif /* !BACKTRACE_H__ */
