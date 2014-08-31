#ifndef BUILDCONFIG_H__
#define BUILDCONFIG_H__

#include "filename.H"
#include "string.H"
#include "wireproto.H"

#define _buildconfigfields(iter, iter1)         \
    iter(string, githead, 1)                    \
    iter(string, gitdiff, 2)                    \
    iter(string, gitlog, 3)                     \
    iter(filename, PREFIX, 4)                   \
    iter(string, CPPFLAGS, 5)                   \
    iter(string, CXXFLAGS, 6)                   \
    iter(string, CFLAGS, 7)                     \
    iter(bool, testing, 8)                      \
    iter1(bool, coverage, 9)
#define buildconfigfields(iter) _buildconfigfields(iter, iter)
class buildconfig {
#define iter(type, name, _) public: const type name;
    buildconfigfields(iter)
#undef iter
public: buildconfig(
#define iter(typ, name, _) const typ &name,
#define iter1(typ, name, _) const typ &name
    _buildconfigfields(iter, iter1)
#undef iter1
#undef iter
    );
public: static const buildconfig us;

public: filename programname(string) const;

    WIREPROTO_TYPE(buildconfig);
};

namespace fields {
class field;
const field &mk(const buildconfig &);
}

#endif /* !BUILDCONFIG_H__ */
