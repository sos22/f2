/* API for managing child processes. */
#ifndef SPAWN_H__
#define SPAWN_H__

#include "either.H"
#include "fd.H"
#include "filename.H"
#include "list.H"
#include "pubsub.H"
#include "shutdown.H"


namespace spawn {

class process;

class program {
    friend class process;
private: filename exec;
private: list<string> args;
public:  program(const filename &);
public:  program &addarg(const char *);
public:  program &addarg(const string &);
public:  program &addarg(string &&);
public:  program &addarg(const fields::field &);
};

/* Ideally, this would inherit privately from iosubscription and
   re-expose the inheritance from subscriptionbase publically, but C++
   doesn't let you do that, so do the whole inheritence publically. */
class subscription : public iosubscription {
private: const process *owner;
public:  subscription(subscriber &, const process &);
public:  void rearm();
public:  ~subscription();
};

class signalnr {
    friend class process;
private: int snr;
private: explicit signalnr(int _snr) : snr(_snr) {}
public:  static const signalnr abort;
public:  static const signalnr cont;
public:  static const signalnr kill;
public:  static const signalnr stop;
public:  static const signalnr term;
public:  bool operator==(signalnr o) const { return snr == o.snr; }
};

class process {
    friend class subscription;
private: int pid; /* really a pid_t, but I don't want to have to grab
                   * the relevant header from here. */
private: fd_t fromchild;
private: fd_t tochild;
private: mutable int nrsubs;
private: mutable mutex_t mux;
private: mutable maybe<either<shutdowncode, signalnr> > res;
private: mutable bool paused;
public:  static orerror<process *> spawn(const program &);
private: process(int pid, fd_t tochild, fd_t fromchild);
public:  void signal(signalnr);
public:  class pausetoken {
        friend class process;
    private: pausetoken() {}
    };
public:  pausetoken pause();
public:  void unpause(pausetoken);
public:  class token {
        friend class process;
    private: token() {}
    };
public:  maybe<token> hasdied() const;
public:  either<shutdowncode, signalnr> join(token);
public:  either<shutdowncode, signalnr> join(clientio);
    /* Equivalent to signal(signalnr::kill) followed by join() */
public:  void kill();
private: ~process() {}
};

}

namespace tests {
void _spawn();
}

#endif /* !SPAWN_H__ */
