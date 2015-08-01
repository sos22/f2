/* The filesystem is a cache recording the content of each storage
 * agent known to the beacon.  It is updated asynchronously, and can
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
/* We do it as a network service, common to a whole cluster, to reduce
 * the room for odd bugs when multiple caches go out of sync with each
 * other. */
#ifndef FILESYSTEMAGENT_H__
#define FILESYSTEMAGENT_H__

class agentname;
class clientio;
class clustername;
template <typename> class nnp;
template <typename> class orerror;
class peername;
class rpcservice2;

orerror<nnp<rpcservice2> > filesystemagent(clientio,
                                           const clustername &cn,
                                           const agentname &an,
                                           const peername &listenon);

#endif /* !FILESYSTEMAGENT_H__ */
