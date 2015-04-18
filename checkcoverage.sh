#! /bin/bash

set -e

module=${1}
report=${2}

t=$(mktemp)
./test2-c --stat ${module} > ${t}
eval $(<${t} sed 's/^[[:space:]]*Line coverage:[[:space:]]* \([0-9.]*\)%/targline=\1/p;d')
eval $(<${t} sed 's/^[[:space:]]*Branch coverage:[[:space:]]*\([0-9.]*\)%/targbranch=\1/p;d')
eval $(head -n 1 ${report})
rm -f ${t}

r=$(echo "${targbranch} <= ${branchcoverage:-0} && ${targline} <= ${linecoverage:-0}" | bc)

if [ $r -eq 1 ]
then
    exit 0
else
    echo "module ${module} failed to achieve its coverage target"
    echo "Target: lines ${targline}%, branches ${targbranch}%"
    echo "Actual: lines ${linecoverage:-0}%, branches ${branchcoverage:-0}%"
    exit 1
fi

