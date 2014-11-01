#ifndef RPCSERVICE2_H__
#define RPCSERVICE2_H__

#include <functional>

#include "listenfd.H"
#include "mutex.H"
#include "peername.H"
#include "proto2.H"

class clientio;
class deserialise1;
class fd_t;
template <typename> class orerror;
template <typename> class maybe;
template <typename> class nnp;
class serialise1;
template <typename> class waitbox;

class rpcservice2config {
public: unsigned maxoutstandingcalls;
public: unsigned txbufferlimit;
public: rpcservice2config(unsigned _maxoutstandingcalls,
                          unsigned _txbufferlimit);
public: static rpcservice2config dflt(); };

class rpcservice2 {
private:   class rootthread;
private:   class connworker;

    friend class rootthread;
    friend class connworker;

private:   const rpcservice2config config;
private:   rootthread *const root;

private:   static orerror<listenfd> open(const peername &pn);

public:    template <typename t, typename ... args> static orerror<t *> listen(
        const peername &,
        args &&...);

public:    class constoken {
        friend class rpcservice2;
    private: constoken() = delete;
    private: constoken(const constoken &) = delete;
    private: listenfd fd;
    private: const peername &pn;
    private: const rpcservice2config &config;
    private: constoken(listenfd _fd,
                       const peername &_pn,
                       const rpcservice2config &_config)
        : fd(_fd),
          pn(_pn),
          config(_config) {} };

protected: explicit rpcservice2(const constoken &);

public:    peername::port port() const;

public:    class onconnectionthread {
        friend class rpcservice2;
    private: onconnectionthread(); };

public:    class incompletecall {
        friend rpcservice2;
    private: incompletecall() = delete;
    private: incompletecall(const incompletecall &) = delete;
    private: void operator=(const incompletecall &) = delete;
    private: incompletecall(connworker &_owner, proto::sequencenr _seqnr);

    private: connworker &owner;
    private: proto::sequencenr const seqnr;

    public:  const waitbox<void> &abandoned() const;
    public:  void complete(
        const std::function<void (serialise1 &s, mutex_t::token txlock)> &doit);
    public:  void complete(
        const std::function<void (serialise1 &s,
                                  mutex_t::token txlock,
                                  onconnectionthread)> &doit,
        onconnectionthread);
    public:  void fail(error);
    public:  void fail(error, onconnectionthread); };
private:   virtual orerror<void> called(
        clientio,
        onconnectionthread,
        deserialise1 &ds,
        nnp<incompletecall> ic) = 0;

    /* Don't care enough to do a non-block version of this right
     * now. */
public:    void destroy(clientio);
    /* Logically private (only called from destroy()) but needs to be
     * protected to keep gcc happy. */
protected: virtual ~rpcservice2(); };


#endif /* !RPCSERVICE2_H__ */
