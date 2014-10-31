#ifndef VERSION_H__
#define VERSION_H__

#include "tuple.H"

class deserialise1;
class serialise1;

#define _version(ctxt, iter0, iter1)                    \
    iter1(ctxt, 1, mand, value, unsigned, v)
class version {
    mktuplefields(version, public)
public: version(deserialise1 &);
public: void serialise(serialise1 &) const;
public: static const version current;
    /* Only really useful in unit tests */
public: static const version invalid;
public: bool operator<(version) const;
public: bool operator>(version) const; };
mktupleext(version)

#endif /* !VERSION_H__ */
