#! /bin/sh

set -e

make -j8 storage
./storage '<storageconfig: beacon=<beaconclientconfig: rs=<registrationsecret:password>> name=<slavename:storagename>>'
