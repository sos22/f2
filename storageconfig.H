#ifndef STORAGECONFIG_H__
#define STORAGECONFIG_H__

#include "beaconclient.H"
#include "filename.H"
#include "slavename.H"
#include "string.H"

class quickcheck;

class storageconfig {
public: string controlsock;
public: filename poolpath;
public: beaconclientconfig beacon;
public: slavename name;
public: storageconfig(const string &_controlsock,
                      const filename &_poolpath,
                      const beaconclientconfig &_beacon,
                      const slavename &_name);
public: storageconfig(const quickcheck &q);
public: bool operator==(const storageconfig &o) const; };

namespace fields {
class field;
const field &mk(const storageconfig &);
}

template <typename> class parser;
namespace parsers {
const parser<storageconfig> &_storageconfig();
}

namespace tests {
void _storageconfig();
}

#endif /* !STORAGECONFIG_H__ */
