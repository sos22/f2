#! /bin/sh

# Simple wrapper script, because writing the masterconfig by hand is a
# bit of a pain.

# (The correct fix is to make the masterconfig parser more forgiving,
# but this'll do for now.)

set -e

make -j8 master
./master '<masterconfig: rs:<registrationsecret:password>>'
