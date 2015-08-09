#ifndef FILESYSTEMCLIENT_H__
#define FILESYSTEMCLIENT_H__

#include "asynccall.H"
#include "connpool.H"
#include "eq.H"

class jobname;

class filesystemclient {
private: class impl;
private: class impl &impl();
private: const class impl &impl() const;
    
public:  static filesystemclient &connect(connpool &cp, const agentname &an);
    
private: class asyncfindjobimpl;
private: struct asyncfindjobdescr {
    typedef list<agentname> _resT;
    typedef filesystemclient _friend;
    typedef asyncfindjobimpl _implT;
    typedef connpool::asynccall::token _innerTokenT; };
public:  typedef asynccall<asyncfindjobdescr> asyncfindjob;
    friend asyncfindjob;
public:  asyncfindjob &findjob(jobname);
public:  orerror<asyncfindjob::resT> findjob(clientio, jobname);
    
private: class asyncstoragebarrierimpl;
private: struct asyncstoragebarrierdescr {
    typedef void _resT;
    typedef filesystemclient _friend;
    typedef asyncstoragebarrierimpl _implT;
    typedef connpool::asynccall::token _innerTokenT; };
public:  typedef asynccall<asyncstoragebarrierdescr> asyncstoragebarrier;
    friend asyncstoragebarrier;
public:  asyncstoragebarrier &storagebarrier(const agentname &,
                                             proto::eq::eventid);
public:  orerror<asyncstoragebarrier::resT> storagebarrier(clientio,
                                                           const agentname &,
                                                           proto::eq::eventid);
    
public:  void destroy(); };

#endif /* !FILESYSTEMCLIENT_H__ */
