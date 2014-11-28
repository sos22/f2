#ifndef WAITQUEUE_H__
#define WAITQUEUE_H__

#include "list.H"
#include "mutex.H"
#include "pubsub.H"

/* Generalisation of waitboxes to represent queues of items rather
 * than a single thing. */
template <typename t> class waitqueue {
private: mutex_t mux;
private: publisher _pub;
private: list<t> _content;
private: list<t> &content(mutex_t::token) { return _content; }
public:  waitqueue()
    : mux(),
      _pub(),
      _content() {}
    
    /* push() and append() are the only ways of adding stuff to the
     * queue.  They are essentially analogous to the list<t> methods
     * push() and append(). Note that, unlike the list<t> variants,
     * these ones don't return references to the pushed thing, because
     * another thread might steal it at any time. */
public:  void push(const t &what) {
    mux.locked([this, &what] (mutex_t::token tok) {
            bool notify(content(tok).empty());
            content(tok).pushtail(what);
            if (notify) _pub.publish(); }); }
public:  template <typename ... args> void append(args && ... params) {
    auto p(list<t>::mkpartial(std::forward<args>(params)...));
    mux.locked([this, &p] (mutex_t::token tok) {
            bool notify(content(tok).empty());
            content(tok).pushtail(p);
            if (notify) _pub.publish(); }); }
    
    /* Check whether the queue is non-empty and, if it is, remove and
     * return the first element.  Otherwise, return Nothing. */
    /* This doesn't used proof-of-work types because they interact
     * badly with uses which have several threads sucking on the same
     * queue. */
public: maybe<t> pop() {
    maybe<t> res(Nothing);
    mux.locked([this, &res] (mutex_t::token tok) {
            if (!content(tok).empty()) res = content(tok).pophead(); });
    return res; }
    
    /* A publisher which is notified at least when pophead() becomes
     * non-Nothing. */
public: const publisher &pub() const { return _pub; }
    
    /* Convenience wrapper around pop() and pub(): wait for something
     * to become available and then pop it. */
public: t pop(clientio io) {
    subscriber sub;
    subscription ss(sub, pub());
    auto tok(mux.lock());
    while (content(tok).empty()) {
        mux.unlock(&tok);
        sub.wait(io);
        tok = mux.lock(); }
    auto res(content(tok).pophead());
    mux.unlock(&tok);
    return res; } };

namespace tests { void _waitqueue(); }

#endif /* !WAITQUEUE_H__ */
