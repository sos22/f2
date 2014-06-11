/* This needs to be included after any headers which define field mk()
   methods, for annoying forward declaration reasons. */
#ifndef FIELDFINAL_H__
#define FIELDFINAL_H__

#include "maybe.H"
#include "orerror.H"

namespace fields {
class field;
template <typename t> const field &
mk(const maybe<t> &m)
{
    if (m.isnothing())
        return mk("Nothing");
    else
        return "<" + mk(m.just()) + ">";
}
template <typename t> const field &
mk(const orerror<t> &m)
{
    if (m.isfailure())
        return "<failed:" + mk(m.failure()) + ">";
    else
        return "<" + mk(m.success()) + ">";
}
};

#endif /* !FIELDFINAL_H__ */
