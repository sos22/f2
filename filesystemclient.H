#ifndef FILESYSTEMCLIENT_H__
#define FILESYSTEMCLIENT_H__

#include "asynccall.H"
#include "connpool.H"
#include "eq.H"
#include "streamstatus.H"

class jobname;

class filesystemclient {
private: class impl;
private: class impl &impl();
private: const class impl &impl() const;
    
public:  static filesystemclient &connect(connpool &cp, const agentname &an);
public:  const agentname &name() const;
    
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
    
private: class asyncfindstreamimpl;
private: struct asyncfindstreamdescr {
    typedef list<pair<agentname, streamstatus> > _resT;
    typedef filesystemclient _friend;
    typedef asyncfindstreamimpl _implT;
    typedef connpool::asynccall::token _innerTokenT; };
public:  typedef asynccall<asyncfindstreamdescr> asyncfindstream;
    friend asyncfindstream;
public:  asyncfindstream &findstream(jobname, const streamname &);
public:  orerror<asyncfindstream::resT> findstream(clientio,
                                                   jobname,
                                                   const streamname &);
    
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

extern template const publisher &
    asynccall<filesystemclient::asyncfindjobdescr>::pub() const;
extern template maybe<asynccall<filesystemclient::asyncfindjobdescr>::token>
    asynccall<filesystemclient::asyncfindjobdescr>::finished() const;
extern template void asynccall<filesystemclient::asyncfindjobdescr>::abort();

#endif /* !FILESYSTEMCLIENT_H__ */
