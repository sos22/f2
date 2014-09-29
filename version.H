#ifndef VERSION_H__
#define VERSION_H__

#include "tuple.H"

#define _version(ctxt, iter0, iter1)                    \
    iter1(ctxt, 1, mand, value, unsigned, v)
class version {
    mktuplefields(version, public)
public: static const version current;
    /* Only really useful in unit tests */
public: static const version invalid; };
mktupleext(version)

#endif /* !VERSION_H__ */