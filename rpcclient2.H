#ifndef RPCCLIENT2_H__
#define RPCCLIENT2_H__

#include "mutex.H"
#include "proto2.H"
#include "pubsub.H"

class deserialise1;
class error;
template <typename> class maybe;
template <typename> class nnp;
template <typename> class orerror;
class peername;
class serialise1;

class rpcclient2 {
public:  template <typename> class asynccall;
public:  class asyncconnect;
private: class workerthread;

private: rpcclient2(const rpcclient2 &) = delete;
private: void operator=(const rpcclient2 &) = delete;

private: nnp<workerthread> worker() const;
private: rpcclient2();

public: static orerror<nnp<rpcclient2> > connect(
    clientio,
    const peername &,
    maybe<timestamp> deadline = Nothing);

public: class asyncconnect {
        friend class rpcclient2;
    private: asyncconnect(const asyncconnect &) = delete;
    private: void operator=(const asyncconnect &) = delete;
    private: asyncconnect();
    private: nnp<workerthread> owner() const;
    public:  class token {
        friend class asyncconnect;
        private: token(); };
    public:  maybe<token> finished() const;
    public:  const publisher &pub() const;
    public:  orerror<nnp<rpcclient2> > pop(token);
    public:  void abort();
    private: ~asyncconnect(); };
public: static nnp<asyncconnect> connect(const peername &);

    /* Tag type used to indicate that a call can hold up the
     * connection state machine if it blocks for too long. */
public: class onconnectionthread {
        friend class rpcclient2;
    private: onconnectionthread(); };

public: template <typename t> orerror<t> call(
    clientio,
    const std::function<void (serialise1 &,
                              mutex_t::token txlock)> &serialise,
    const std::function<orerror<t> (deserialise1 &,
                                    onconnectionthread)> &deserialise,
    maybe<timestamp> deadline = Nothing);

    /* Bits which are common to every possible return type for
     * asynccalls. */
private: class _asynccall {
    public:    mutable mutex_t mux;
    public:    bool aborted;
    public:    bool _finished;
    public:    publisher _pub;
    public:    proto::sequencenr seqnr;
    public:    virtual void complete(deserialise1 &, onconnectionthread) = 0;
    public:    virtual void fail(error, onconnectionthread) = 0;
    public:    class token {
            friend class _asynccall;
        private: token() {}; };
    public:    maybe<token> finished() const;
    public:    token wait(clientio) const;
    protected: ~_asynccall(); };
public: template <typename t> class asynccall final : private _asynccall {
        friend class rpcclient2;
    private: asynccall() = delete;
    private: asynccall(const asynccall &) = delete;
    private: maybe<orerror<t> > res;
    private: std::function<orerror<t> (class asynccall<t> &,
                                       orerror<nnp<deserialise1> >,
                                       onconnectionthread)> deserialise;

    private: asynccall(
        const std::function<orerror<t> (class asynccall<t> &,
                                        orerror<nnp<deserialise1> >,
                                        onconnectionthread)> &);

    private: void complete(deserialise1 &, onconnectionthread oct);
    private: void fail(error, onconnectionthread oct);
    public:  typedef _asynccall::token token;
    public:  maybe<token> finished() const { return _asynccall::finished(); }
    public:  const publisher &pub() const { return _pub; }
    public:  orerror<t> pop(token);
    public:  maybe<orerror<t> > abort(clientio);
    public:  orerror<t> pop(clientio io) { return pop(wait(io)); }
    private: ~asynccall() {} };
public:  template <typename t> nnp<asynccall<t> > call(
        const std::function<void (serialise1 &, mutex_t::token)> &serialise,
        const std::function<orerror<t> (class asynccall<t> &,
                                        orerror<nnp<deserialise1> >,
                                        onconnectionthread)> &deserialise);

public:  ~rpcclient2(); };

template <> maybe<rpcclient2::asynccall<void>::token>
    rpcclient2::asynccall<void>::finished() const;

#endif /* !RPCCLIENT2_H__ */
