#! /bin/sh

set -e

make -j8 storageservice
./storageservice '<storageconfig: beacon:<beaconserverconfig: cluster:<clustername:foo> name:<agentname:storage>>>'
