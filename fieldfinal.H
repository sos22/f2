/* This needs to be included after any headers which define field mk()
   methods, for annoying forward declaration reasons. */
#ifndef FIELDFINAL_H__
#define FIELDFINAL_H__

#include "list.H"
#include "maybe.H"
#include "orerror.H"

namespace fields {
class field;
const field &mk(const char *);
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
template <typename t> const field &
mk(const t *const &what)
{
    if (!what)
        return fields::mk("NULL");
    else
        return "[" + fields::mk(*what) + "]";
}
template <typename t> const field &
mk(const list<t> &what) {
    const field *acc = &mk("{");
    bool first = true;
    for (auto it(what.start()); !it.finished(); it.next()) {
        if (!first) acc = &(*acc + " ");
        first = false;
        acc = &(*acc + mk(*it)); }
    return *acc + "}"; }

};

#endif /* !FIELDFINAL_H__ */
