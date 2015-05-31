#ifndef STORAGECONFIG_H__
#define STORAGECONFIG_H__

#include "beaconserver.H"
#include "filename.H"

template <typename> class parser;

class storageconfig {
public: const filename poolpath;
public: const beaconserverconfig beacon;
public: storageconfig(const filename &, const beaconserverconfig &);
public: storageconfig(const quickcheck &);
public: bool operator==(const storageconfig &o) const;
public: bool operator!=(const storageconfig &o) const {
    return !(*this == o); }
public: const fields::field &field() const;
public: static const ::parser<storageconfig> &parser(); };

#endif /* !STORAGECONFIG_H__ */
