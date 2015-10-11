/* Client library for the storage interface. */
#ifndef STORAGECLIENT_H__
#define STORAGECLIENT_H__

#include "agentname.H"
#include "asynccall.H"
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
private: storageclient() {}
private: storageclient(const storageclient &) = delete;
private: void operator=(const storageclient &) = delete;
    
public:  static storageclient &connect(connpool &cp, const agentname &an);
    
private: class asynccreatejobimpl;
private: struct asynccreatejobdescr {
    typedef proto::eq::eventid _resT;
    typedef storageclient _friend;
    typedef asynccreatejobimpl _implT;
    typedef connpool::asynccallT<proto::eq::eventid>::token _innerTokenT; };
public:  typedef asynccall<asynccreatejobdescr> asynccreatejob;
    friend asynccreatejob;
public:  asynccreatejob &createjob(const job &);
public:  orerror<asynccreatejob::resT> createjob(clientio, const job &);
    
private: class asyncappendimpl;
private: struct asyncappenddescr {
    typedef void _resT;
    typedef storageclient _friend;
    typedef asyncappendimpl _implT;
    typedef connpool::asynccall::token _innerTokenT; };
public:  typedef asynccall<asyncappenddescr> asyncappend;
    friend asyncappend;
public:  asyncappend &append(jobname,
                             const streamname &,
                             const buffer &,
                             bytecount);
public:  orerror<asyncappend::resT> append(clientio,
                                           jobname,
                                           const streamname &,
                                           const buffer &,
                                           bytecount);
public:  asyncappend &append(jobname,
                             const streamname &,
                             _Steal,
                             buffer &,
                             bytecount);
public:  orerror<asyncappend::resT> append(clientio,
                                           jobname,
                                           const streamname &,
                                           _Steal,
                                           buffer &,
                                           bytecount);
    
private: class asyncfinishimpl;
private: struct asyncfinishdescr {
    typedef proto::eq::eventid _resT;
    typedef storageclient _friend;
    typedef asyncfinishimpl _implT;
    typedef connpool::asynccallT<_resT>::token _innerTokenT; };
public:  typedef asynccall<asyncfinishdescr> asyncfinish;
    friend asyncfinish;
public:  asyncfinish &finish(jobname, const streamname &);
public:  orerror<asyncfinish::resT> finish(clientio,
                                           jobname,
                                           const streamname &);
    
private: class asyncreadimpl;
private: struct asyncreaddescr {
    typedef pair<bytecount, buffer> _resT;
    typedef storageclient _friend;
    typedef asyncreadimpl _implT;
    typedef connpool::asynccallT<bytecount>::token _innerTokenT; };
public:  typedef asynccall<asyncreaddescr> asyncread;
    friend asyncread;
public:  asyncread &read(jobname,
                         const streamname &,
                         maybe<bytecount> start = Nothing,
                         maybe<bytecount> end = Nothing);
public:  orerror<asyncread::resT> read(clientio,
                                       jobname,
                                       const streamname &,
                                       maybe<bytecount> start = Nothing,
                                       maybe<bytecount> end = Nothing);
    
private: class asynclistjobsimpl;
private: struct asynclistjobsdescr {
    typedef pair<proto::eq::eventid, list<jobname> > _resT;
    typedef storageclient _friend;
    typedef asynclistjobsimpl _implT;
    typedef connpool::asynccallT<proto::storage::listjobsres>::token
        _innerTokenT; };
public:  typedef asynccall<asynclistjobsdescr> asynclistjobs;
    friend asynclistjobs;
public:  asynclistjobs &listjobs();
public:  orerror<asynclistjobs::resT> listjobs(clientio);
    
private: class asyncstatjobimpl;
private: struct asyncstatjobdescr {
    typedef job _resT;
    typedef storageclient _friend;
    typedef asyncstatjobimpl _implT;
    typedef connpool::asynccallT<job>::token _innerTokenT; };
public:  typedef asynccall<asyncstatjobdescr> asyncstatjob;
    friend asyncstatjob;
public:  asyncstatjob &statjob(jobname);
public:  orerror<asyncstatjob::resT> statjob(clientio, jobname);
    
private: class asyncliststreamsimpl;
private: struct asyncliststreamsdescr {
    typedef pair<proto::eq::eventid, list<streamstatus> > _resT;
    typedef storageclient _friend;
    typedef asyncliststreamsimpl _implT;
    typedef connpool::asynccallT<proto::storage::liststreamsres>::token
        _innerTokenT; };
public:  typedef asynccall<asyncliststreamsdescr> asyncliststreams;
    friend asyncliststreams;
public:  asyncliststreams &liststreams(jobname);
public:  orerror<asyncliststreams::resT> liststreams(clientio, jobname);
    
private: class asyncstatstreamimpl;
private: struct asyncstatstreamdescr {
    typedef streamstatus _resT;
    typedef storageclient _friend;
    typedef asyncstatstreamimpl _implT;
    typedef connpool::asynccallT<streamstatus>::token _innerTokenT; };
public:  typedef asynccall<asyncstatstreamdescr> asyncstatstream;
    friend asyncstatstream;
public:  asyncstatstream &statstream(jobname, const streamname &);
public:  orerror<asyncstatstream::resT> statstream(
    clientio,
    jobname,
    const streamname &);
    
private: class asyncremovejobimpl;
private: struct asyncremovejobdescr {
    typedef proto::eq::eventid _resT;
    typedef storageclient _friend;
    typedef asyncremovejobimpl _implT;
    typedef connpool::asynccallT<_resT>::token _innerTokenT; };
public:  typedef asynccall<asyncremovejobdescr> asyncremovejob;
    friend asyncremovejob;
public:  asyncremovejob &removejob(jobname);
public:  orerror<asyncremovejob::resT> removejob(clientio, jobname);
    
public:  void destroy(); };

#endif /* !STORAGECLIENT_H__ */
