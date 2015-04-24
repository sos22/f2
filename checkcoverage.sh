#! /bin/bash

set -e

if [ $# -ne 1 ]
then
    echo "Need a single argument, the test report to analyse"
    exit 1
fi

report=${1}

eval $(head -n 1 ${report})
t=$(mktemp)
./test2-c --stat ${module} > ${t}
eval $(<${t} sed 's/^[[:space:]]*Line coverage:[[:space:]]* \([0-9.]*\)%/targline=\1/p;d')
eval $(<${t} sed 's/^[[:space:]]*Branch coverage:[[:space:]]*\([0-9.]*\)%/targbranch=\1/p;d')
rm -f ${t}

r=$(echo "${targbranch}*0.999 <= ${branchcoverage:-0} && ${targline}*.999 <= ${linecoverage:-0}" | bc)

if [ $r -eq 1 ]
then
    exit 0
else
    echo "module ${module} failed to achieve its coverage target"
    echo "Target: lines ${targline}%, branches ${targbranch}%"
    echo "Actual: lines ${linecoverage:-0}%, branches ${branchcoverage:-0}%"
    exit 1
fi

