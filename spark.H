/* Sparks are a kind of short-lived thread which compute some value,
   return it, and then immediately exit.  They should not be used for
   long-running computation. */
#ifndef SPARK_H__
#define SPARK_H__

#include "clientio.H"
#include "thread2.H"

class publisher;
template <typename> class waitbox;

template <typename t>
class spark {
private: class thr : public thread2 {
        friend class thread2;
    private: spark *owner;
    private: std::function<t ()> what;
    private: thr(thread2::constoken,
                 spark *,
                 const std::function<t ()> &);
    private: void run(clientio);
    };
private: waitbox<t> wb;
private: thr *th;
public:  spark(const std::function<t ()> &what);
public:  bool ready() const { return wb.ready(); }
    /* sparked functions don't get clientio tokens, so must complete
       quickly, and so we know that wb.get() is safe and spark::get()
       doesn't need an additional clientio token. */
public:  const t &get() const { return wb.get(clientio::CLIENTIO); }
public:  ~spark();
};

#endif /* !SPARK_H__ */
