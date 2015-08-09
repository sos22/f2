#ifndef COMPUTECLIENT_H__
#define COMPUTECLIENT_H__

#include "asynccall.H"
#include "connpool.H"
#include "compute.H"
#include "eq.H"

class computeclient {
private: class impl;
private: class impl &impl();
private: const class impl &impl() const;
    
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

public:  void destroy(); };

#endif /* !COMPUTECLIENT_H__ */
