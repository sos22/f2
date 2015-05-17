/* Client library for the storage interface. */
#ifndef STORAGECLIENT_H__
#define STORAGECLIENT_H__

#include "agentname.H"
#include "connpool.H"
#include "eq.H"
#include "job.H"
#include "storage.H"

class jobname;
template <typename> class list;
template <typename> class maybe;
template <typename> class nnp;
template <typename> class orerror;
class publisher;

class storageclient {
private: class impl;
private: class impl &impl();
private: const class impl &impl() const;

private: template <typename _resT,
                   typename _implT,
                   typename _innerTokenT> class asynccall {
        friend class storageclient;
    public:    typedef _resT resT;
    public:    typedef _implT implT;
    public:    typedef _innerTokenT innerTokenT;
    protected: asynccall() {}
    private:   asynccall(const asynccall &) = delete;
    private:   void operator=(const asynccall &) = delete;
    protected: ~asynccall() {}
    private:   implT &impl();
    private:   const implT &impl() const;
    public:    class token {
            friend asynccall;
        private: innerTokenT inner;
        private: token(innerTokenT _inner) : inner(_inner) {} };
    public:    maybe<token> finished() const;
    public:    const publisher &pub() const;
    public:    orerror<resT> pop(token t);
    public:    orerror<resT> pop(clientio io);
    public:    void abort(); };

private: class asyncconnectimpl;
public:  typedef asynccall<nnp<storageclient>,
                           asyncconnectimpl,
                           connpool::asynccall::token> asyncconnect;
public:  static asyncconnect &connect(connpool &cp, const agentname &an);
public:  static orerror<asyncconnect::resT> connect(
    clientio io,
    connpool &cp,
    const agentname &an);
    
private: class asynccreatejobimpl;
public:  typedef asynccall<proto::eq::eventid,
                           asynccreatejobimpl,
                           connpool::asynccallT<proto::eq::eventid>::token>
             asynccreatejob;
public:  asynccreatejob &createjob(const job &);
public:  orerror<asynccreatejob::resT> createjob(clientio, const job &);
    
private: class asynclistjobsimpl;
public:  typedef asynccall<
    pair<proto::eq::eventid, list<jobname> >,
    asynclistjobsimpl,
    connpool::asynccallT<proto::storage::listjobsres>::token> asynclistjobs;
public:  asynclistjobs &listjobs();
public:  orerror<asynclistjobs::resT> listjobs(clientio);
    
private: class asyncstatjobimpl;
public:  typedef asynccall<
    job,
    asyncstatjobimpl,
    connpool::asynccallT<job>::token> asyncstatjob;
public:  asyncstatjob &statjob(jobname);
public:  orerror<asyncstatjob::resT> statjob(clientio, jobname);
    
private: class asyncliststreamsimpl;
public:  typedef asynccall<
    pair<proto::eq::eventid, list<streamstatus> >,
    asyncliststreamsimpl,
    connpool::asynccallT<proto::storage::liststreamsres>::token>
        asyncliststreams;
public:  asyncliststreams &liststreams(jobname);
public:  orerror<asyncliststreams::resT> liststreams(clientio, jobname);
    
private: class asyncremovejobimpl;
public:  typedef asynccall<
    void,
    asyncremovejobimpl,
    connpool::asynccallT<void>::token> asyncremovejob;
public:  asyncremovejob &removejob(jobname);
public:  orerror<asyncremovejob::resT> removejob(clientio, jobname);
    
public:  void destroy(); };

#endif /* !STORAGECLIENT_H__ */