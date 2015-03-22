#! /bin/sh

set -e

make -j8 computeclient testjob.so
./computeclient '<clustername:foo>' '<agentname:compute>' START '<job:<filename:"./testjob.so">:testfunction ->output1>'
