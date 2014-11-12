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

namespace params {
#define iter(type, name, i) static const wireproto::parameter<type> name(i);
buildconfigfields(iter)
#undef iter
}

void
buildconfig::addparam(
    wireproto::parameter<buildconfig> tmpl,
    wireproto::tx_message &tx_msg) const {
    tx_msg.addparam(wireproto::parameter<wireproto::tx_compoundparameter>(tmpl),
                    wireproto::tx_compoundparameter()
#define dofield(type, name, i) .addparam(params::name, name)
                    buildconfigfields(dofield)
#undef dofield
                    ); }

maybe<buildconfig>
buildconfig::fromcompound(const wireproto::rx_message &rxm) {
#define dofield(type, name, i)                  \
    auto _ ##name(rxm.getparam(params::name));  \
    if (_ ##name == Nothing) return Nothing;    \
    auto name(_##name.just());
    buildconfigfields(dofield);
#undef dofield
    return buildconfig(
#define iter(type, name, i) name,
#define iter1(type, name, i) name
        _buildconfigfields(iter, iter1)
#undef iter1
#undef iter
        ); }

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
