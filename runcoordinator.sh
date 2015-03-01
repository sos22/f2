#! /bin/sh

set -e
make -j8 coordinatorservice
./coordinatorservice '<clustername:foo>' '<agentname:coordinator>' '<agentname:filesystem>'
