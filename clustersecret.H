#ifndef CLUSTERSECRET_H__
#define CLUSTERSECRET_H__

#include "string.H"
#include "tuple.H"

#define _clustersecret(ctxt, iter0, iter1)      \
    iter1(ctxt, ref, 1, string, secret)
class clustersecret {
    mktuplefields(clustersecret, private)
public:  static maybe<clustersecret> mk(const string &); };
mktuplefieldext(clustersecret);

#endif /* !CLUSTERSECRET_H__ */
