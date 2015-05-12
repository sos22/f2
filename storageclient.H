/* Client library for the storage interface. */
#ifndef STORAGECLIENT_H__
#define STORAGECLIENT_H__

#include "agentname.H"
#include "connpool.H"
#include "eq.H"

class job;
template <typename> class list;
template <typename> class maybe;
template <typename> class nnp;
template <typename> class orerror;
class publisher;

class storageclient {
private: class impl;
private: class impl &impl();
private: const class impl &impl() const;
    
public:  class asyncconnect {
        friend class storageclient;
    private: asyncconnect() {}
    private: asyncconnect(const asyncconnect &) = delete;
    private: void operator=(const asyncconnect &) = delete;
    private: ~asyncconnect() {}
    private: class impl;
    private: class impl &impl();
    private: const class impl &impl() const;
    public:  typedef orerror<nnp<storageclient> > resT;
    public:  class token {
            friend class impl;
        private: connpool::asynccall::token inner;
        private: token(connpool::asynccall::token _inner) : inner(_inner) {} };
    public:  maybe<token> finished() const;
    public:  const publisher &pub() const;
    public:  resT pop(token t);
    public:  void abort(); };
public:  static asyncconnect &connect(
    connpool &cp,
    const agentname &an);
public:  static asyncconnect::resT connect(
    clientio,
    connpool &,
    const agentname &);
    
public:  class asynccreatejob {
        friend class storageclient;
    private: asynccreatejob() {}
    private: asynccreatejob(const asynccreatejob &) = delete;
    private: void operator=(const asynccreatejob &) = delete;
    private: ~asynccreatejob() {}
    private: class impl;
    private: class impl &impl();
    private: const class impl &impl() const;
    public:  typedef orerror<proto::eq::eventid> resT;
    public:  class token {
            friend class asynccreatejob;
        private: connpool::asynccallT<proto::eq::eventid>::token inner;
        private: token(connpool::asynccallT<proto::eq::eventid>::token _inner)
            : inner(_inner) {} };
    public:  maybe<token> finished() const;
    public:  const publisher &pub() const;
    public:  resT pop(token t);
    public:  void abort(); };
public:  asynccreatejob &createjob(const job &);
public:  asynccreatejob::resT createjob(clientio, const job &);
    
public:  void destroy(); };

#endif /* !STORAGECLIENT_H__ */
