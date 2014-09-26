#ifndef CLUSTERNAME_H__
#define CLUSTERNAME_H__

#include "string.H"
#include "tuple.H"

#define _clustername(ctxt, iter0, iter1)        \
    iter1(ctxt, 1, mand, ref, ::string, value)
class clustername {
    mktuplefields(clustername, private)
    /* Limit cluster name size to reduce risk of hitting wireproto
     * message size limits. */
public:  static const unsigned maxsize;
public:  static maybe<clustername> mk(const string &); };
mktupleext(clustername);

#endif /* !CLUSTERNAME_H__ */
