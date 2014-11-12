#include "buildconfig.H"

#include "fields.H"

buildconfig::buildconfig(
#define iter(type, name, ignore) const type &_ ## name ,
#define iter1(type, name, ignore) const type &_ ## name
    _buildconfigfields(iter, iter1)
#undef iter
#undef iter1
    )
    :
#define iter(type, name, ignore) name(_ ## name),
#define iter1(type, name, ignore) name(_ ## name)
    _buildconfigfields(iter, iter1)
#undef iter
#undef iter1
    {}

filename
buildconfig::programname(string x) const {
    return PREFIX + (x + string(coverage ? "-c" : "")); }

const fields::field &
fields::mk(const buildconfig &c) {
    const fields::field *acc(&fields::mk("<buildconfig:"));
#define iter(type, name, i)                     \
    acc = &(*acc + " "#name":" + mk(c.name));
    buildconfigfields(iter);
#undef iter
    return *acc + ">"; }
