#! /bin/sh

set -e
make -j8 filesystemservice
./filesystemservice '<clustername:foo>' '<agentname:filesystem>'
