#! /bin/sh

set -e

make -j8 computeservice
./computeservice '<clustername:foo>' '<agentname:compute>'
