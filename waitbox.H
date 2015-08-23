#ifndef POLLABLE_COND_H__
#define POLLABLE_COND_H__

#include "maybe.H"
#include "mutex.H"
#include "pubsub.H"

class clientio;

/* A box you can put a value in, and some gubbins to wait until
   someone puts a value in it.  */
template <typename t>
class waitbox {
private: mutable mutex_t mux; /* mutable because we need to acquire it
                               * from the const ready() method. */
private: maybe<t> content;
private: publisher pub_;
public:  waitbox() : mux(), content(Nothing) {};
public:  const publisher &pub() { return pub_; }

    /* Set the waitbox.  Must be called at most once. */
public:  void set(const t &what);
    /* Wait for the waitbox to be set and return its contents. */
public:  const t &get(clientio) const;
    /* Wait for the waitbox to be set and return its contents using a
     * steal constructor. */
public:  t get(_Steal, clientio);
    /* Return the contents of the waitbox if it's been set and Nothing
     * otherwise. */
public:  maybe<t> poll() const;
    /* Check whether the waitbox is set, returning true if it is and
       false otherwise. */
public:  bool ready() const; };

/* Special case for void. */
template <>
class waitbox<void> {
private: mutable mutex_t mux;
private: maybe<void> content;
private: publisher pub_;
public:  waitbox() : mux(), content(Nothing) {}
public:  const publisher &pub() const { return pub_; }
public:  void set();
public:  void get(clientio) const;
public:  maybe<void> get(clientio, timestamp) const;
public:  bool ready() const; };

#endif
