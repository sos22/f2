/* The filesystem is a cache recording the content of each storage
 * slave known to the beacon.  It is updated asynchronously, and can
 * therefore lag behind reality, and can even occasionally reorder
 * events.  It does, however, give users a couple of useful guarantees:
 *
 * -- It preserves monotonicity for individual objects (jobs and
 *    streams), in the sense that if an object is seen going from
 *    state X to state Y in the filesystem then it must have gone from
 *    X to Y in reality.  It may sometimes combine edges, so if the
 *    real object transitions X to Y to Z then the filesystem object
 *    might go directly from X to Z, but that should be
 *    indistinguishable from the user just not getting scheduled in
 *    the right place.
 * -- The system is eventually consistent, so any change in the real
 *    world will eventually be reflected in the cache, and if the real
 *    world stops changing for long enough the cache will eventually
 *    converge on the truth.  There is no hard deadline for
 *    convergence, but under normal load it should be at most a few
 *    seconds, and will often be quicker.
 *
 * Note, though, that the cache does *not* preserve ordering of state
 * changes on different objects.  So, for instance, if in the real
 * world someone creates stream X and then stream Y the cache might
 * notice the creation of Y before the creation of X.
 */
#ifndef FILESYSTEM_H__
#define FILESYSTEM_H__

#include "eq.H"

class beaconclient;
class connpool;
class jobname;
template <typename> class list;
template <typename> class maybe;
template <typename, typename> class pair;
class slavename;
class streamname;
class streamstatus;

class filesystem {
    /* Cannot have any member fields because the destructor will never
     * be invoked. */
private: class impl;
private: const impl &implementation() const;
private: impl &implementation();
public:  static filesystem &build(connpool &, beaconclient &);
public:  list<slavename> findjob(const jobname &jn) const;
public:  list<pair<slavename, streamstatus> > findstream(
    const jobname &jn,
    const streamname &sn) const;
public:  maybe<slavename> nominateslave() const;

    /* Tell the filesystem that something has created a new job. It
     * should pick it up eventually anyway; the only reason to call
     * this is to encourage it to pick up the creation quickly.  In
     * particular, callers can safely assume that by the time this
     * returns the cache will have been updated to reflect the new
     * job. */
public:  void newjob(const slavename &, const jobname &, proto::eq::eventid); };

#endif /* !FILESYSTEM_H__ */
