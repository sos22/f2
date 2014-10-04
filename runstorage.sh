#! /bin/sh

set -e

make -j8 storage
./storage '<storageconfig: beacon:<beaconserverconfig: cluster:<clustername:foo> name:<slavename:storage>>>'
