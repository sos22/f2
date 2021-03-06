#ifndef COMPUTECLIENT_H__
#define COMPUTECLIENT_H__

#include "asynccall.H"
#include "connpool.H"
#include "compute.H"
#include "eq.H"

class computeclient {
private: class impl;
private: class impl &impl();
    
public:  static computeclient &connect(connpool &cp, const agentname &an);
    
private: class startimpl;
private: struct startdescr {
    typedef pair<proto::eq::eventid, proto::compute::tasktag> _resT;
    typedef computeclient _friend;
    typedef startimpl _implT;
    typedef connpool::asynccall::token _innerTokenT; };
public:  typedef asynccall<startdescr> asyncstart;
    friend asyncstart;
public:  asyncstart &start(const job &);
public:  orerror<startdescr::_resT> start(clientio, const job &);
    
private: class enumerateimpl;
private: struct enumeratedescr {
    typedef pair<proto::eq::eventid, list<proto::compute::jobstatus> > _resT;
    typedef computeclient _friend;
    typedef enumerateimpl _implT;
    typedef connpool::asynccall::token _innerTokenT; };
public:  typedef asynccall<enumeratedescr> asyncenumerate;
    friend asyncenumerate;
public:  asyncenumerate &enumerate(void);
public:  orerror<enumeratedescr::_resT> enumerate(clientio);
    
private: class dropimpl;
private: struct dropdescr {
    typedef void _resT;
    typedef computeclient _friend;
    typedef dropimpl _implT;
    typedef connpool::asynccall::token _innerTokenT; };
public:  typedef asynccall<dropdescr> asyncdrop;
    friend asyncdrop;
public:  asyncdrop &drop(const jobname &);
public:  orerror<dropdescr::_resT> drop(clientio, const jobname &);
    
    /* Wait until a given job finishes, returning its result. */
    /* The weird double-orerror<> construct is to distinguish between
     * errors generated by the job (the inner orerror) and those
     * generated fetching the job result (the outer one). */
public:  class asyncwaitjob {
        friend class computeclient;
    private: class impl;
    private: const class impl &impl() const;
    private: class impl &impl();
    private: asyncwaitjob() {}
    public:  class token {
        friend class asyncwaitjob;
        private: token() {} };
    public:  maybe<token> finished() const;
    public:  const publisher &pub() const;
    public:  orerror<orerror<jobresult> > pop(token);
    public:  orerror<orerror<jobresult> > pop(clientio);
    public:  void abort(); };
public:  asyncwaitjob &waitjob(jobname);
public:  orerror<orerror<jobresult> > waitjob(clientio, jobname);
    
    /* Wrapper around start(), waitjob(), and drop(): kick off a job,
     * wait for it to complete, and then drop it from the table. */
public:  orerror<orerror<jobresult> > runjob(clientio, const job &);
    
public:  void destroy(); };

#endif /* !COMPUTECLIENT_H__ */
