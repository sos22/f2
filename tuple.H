/* Horrible pile of macro gunk to make defining tuple-like types a bit
 * easier.  The idea is that you define a type defining macro and the
 * tuple macros then invoke it with various different iterators to
 * build a simple tuple-like type.  So, for instance, if you want a
 * tuple called T with fields int x, char y, string z, you'd do
 * something like this:

 #define _T(ctxt, iter0, iter1)                 \
 iter0(ctxt, 1, opt, value, int, x)             \
 iter0(ctxt, 2, mand, value, char, y)           \
 iter1(ctxt, 3, opt, ref, string, z)

 * The first argument to the iterators is a context which should be
 * passed in verbatim from the type definer.  The second is an index
 * which must be unique for each field.  The third is an indicator,
 * either mand or opt, of whether the parameter should be considered
 * optional.  The fourth is an indicator, either value or ref, which
 * is used to decide whether to pass a field by reference or by value.
 * The fifth is the type of the field. The sixth is the name of the
 * field.  Use the first iterator for ``internal'' fields and the
 * second for the last in the structure.
 *
 * You can then declare the tuple like this:

class T {
mktuplefields(T, public)
};
mktupleext(T)

 * And define it like this:

mktupledef(T)

 * The declaration is split into two bits so that you can add extra
 * methods to the field class.
 *
 * It may be horrible, but it's a fair bit less horrible than doing
 * the same things by hand.
 */
#ifndef TUPLE_H__
#define TUPLE_H__

#include "fields.H"
#include "quickcheck.H"

#define __mktuplefieldsopt(type)                                        \
    maybe< type >
#define __mktuplefieldsmand(type)                                       \
    type
#define __mktuplefields(_ctxt, _idx, opt, _flavour, type, name)         \
    public: __mktuplefields ## opt(type) name ;
#define __mktupledeclrefopt(type, fname)                                \
    const maybe< type > &fname
#define __mktupledeclrefmand(type, fname)                               \
    const type &fname
#define __mktupledeclvalueopt(type, fname)                              \
    maybe< type > fname
#define __mktupledeclvaluemand(type, fname)                             \
    type fname
#define __mktupledecl0(_ctxt, _idx, opt, flavour, type, fname)          \
    __mktupledecl ## flavour ## opt(type, _##fname),
#define __mktupledecl1(_ctxt, _idx, opt, flavour, type, fname)          \
    __mktupledecl ## flavour ## opt(type, _##fname)
#define mktuplefields(cname, cvis)                                      \
        _ ## cname(Z, __mktuplefields, __mktuplefields)                 \
public: cname(const quickcheck &q);                                     \
cvis: explicit cname(_ ## cname (Z, __mktupledecl0, __mktupledecl1));   \
public: bool operator==(const cname &) const;                           \
public: bool operator!=(const cname &) const;

#define mktupleext(name)                                                \
namespace fields {                                                      \
class field;                                                            \
const field &mk(const name &); }                                        \
template <typename> class parser;                                       \
namespace parsers {                                                     \
const parser<name> &__ ##name();}

#define __mktupleconstructor0(_ctxt, _idx, _opt, _flavour, _type, fname) \
    fname(_##fname),
#define __mktupleconstructor1(_ctxt, _idx, _opt, _flavour, _type, fname) \
    fname(_##fname)
#define __mktupleconstructorq0(_ctxt, _idx, _opt, _flavour, _type, name) \
    name(q),
#define __mktupleconstructorq1(_ctxt, _idx, _opt, _flavour, _type, name) \
    name(q)
#define __mktuplene(_ctxt, _idx, _opt,_flavour, _type, name)            \
    if (!(name == o.name)) return true;
#define __mktuplefield(_ctxt, _idx, _opt, _flavour, _type, name)        \
    + " " #name ":" + fields::mk(o.name).escape()
#define mktupledef(cname)                                               \
cname::cname(_ ## cname (Z, __mktupledecl0, __mktupledecl1))            \
    : _ ##cname (Z, __mktupleconstructor0, __mktupleconstructor1) {}    \
cname::cname(const quickcheck &q)                                       \
    : _ ##cname(Z, __mktupleconstructorq0, __mktupleconstructorq1) {}   \
bool                                                                    \
cname::operator!=(const cname &o) const {                               \
    _ ##cname (Z, __mktuplene, __mktuplene);                            \
    return false; }                                                     \
bool                                                                    \
cname::operator==(const cname &o) const {                               \
    return !(*this != o); }                                             \
const fields::field &                                                   \
fields::mk(const cname &o) {                                            \
    return fields::mk("<" #cname ":")                                   \
        _ ## cname(Z, __mktuplefield, __mktuplefield)                   \
        + ">"; }

#endif /* !TUPLE_H__ */
