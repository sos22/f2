/* Sparks are a kind of short-lived thread which compute some value,
   return it, and then immediately exit.  They should not be used for
   long-running computation. */
#ifndef SPARK_H__
#define SPARK_H__

#include "thread.H"

class clientio;
class publisher;
template <typename> class waitbox;

template <typename t>
class spark : private threadfn {
private: waitbox<t> wb;
private: thread *thr;
private: std::function<t ()> what;
public:  const publisher &pub;
public:  spark(std::function<t ()>);
private: void run(clientio);
public:  bool ready() const { return wb.ready(); }
public:  const t &get() const { return wb.get(); }
public:  ~spark();
};

#endif /* !SPARK_H__ */