#! /bin/bash

set -e

if [ $# -ne 1 ]
then
    echo "Need a single argument, the test report to analyse"
    exit 1
fi

report=${1}

make -j8 -s test2-c

eval $(head -n 1 ${report})
t=$(mktemp)
./test2-c --stat ${module} > ${t}
eval $(<${t} sed 's/^[[:space:]]*Line coverage:[[:space:]]* \([0-9.]*\)%/targline=\1/p;d')
eval $(<${t} sed 's/^[[:space:]]*Branch coverage:[[:space:]]*\([0-9.]*\)%/targbranch=\1/p;d')
rm -f ${t}

r1=$(echo "${targline}*.999 <= ${linecoverage:-0} && ${targline} >= ${linecoverage:-0}*.85" | bc)
r2=$(echo "${targbranch}*0.999 <= ${branchcoverage:-0} && ${targbranch} >= ${branchcoverage:-0}*.85" | bc)

if [ $r1 -eq 1 ] && [ $r2 -eq 1 ]
then
    exit 0
else
    echo "module ${module} failed to achieve its coverage target"
    echo "Target: lines ${targline}%, branches ${targbranch}%"
    echo "Actual: lines ${linecoverage:-0}%, branches ${branchcoverage:-0}%"
    exit 1
fi

