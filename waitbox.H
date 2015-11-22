#ifndef POLLABLE_COND_H__
#define POLLABLE_COND_H__

#include "maybe.H"
#include "mutex.H"
#include "pubsub.H"

class clientio;

namespace fields { class field; }

/* A box you can put a value in, and some gubbins to wait until
   someone puts a value in it.  */
template <typename t>
class waitbox {
private: mutable mutex_t mux; /* mutable because we need to acquire it
                               * from the const ready() method. */
private: maybe<t> content;
private: publisher pub_;
public:  waitbox() : mux(), content(Nothing) {};
public:  const publisher &pub() const { return pub_; }

    /* Set the waitbox.  Must be called at most once. */
public:  void set(const t &what);
    /* Set the waitbox if it's currently empty. */
public:  void setif(const t &what);
    /* Wait for the waitbox to be set and return its contents. */
public:  const t &get(clientio) const;
    /* Wait for the waitbox to be set and return its contents, or
     * Nothing if we hit the timeout. */
public:  maybe<t> get(clientio, timestamp deadline) const;
    /* Wait for the waitbox to be set and return its contents using a
     * steal constructor. */
public:  t get(_Steal, clientio);
    /* Return the contents of the waitbox if it's been set and Nothing
     * otherwise. */
public:  maybe<t> poll() const;
    /* Check whether the waitbox is set, returning true if it is and
       false otherwise. */
public:  bool ready() const;
    /* Field representation of the box's contents. */
public:  const fields::field &field() const; };

/* Special case for void. */
template <>
class waitbox<void> : public waitbox<Void> {
public:  void set(); };

#endif
