#! /bin/bash

set -e

# Run all of the defined test modules, recording their results.  Fail
# if any of them fail (including coverage failures)

outdir=$1
mkdir ${outdir}
trap "rm -rf ${outdir}" EXIT

make -j8 test2-c

# summary1 has one line for every individual test.  The first field of
# the line is a sort key and the rest is the message itself.

# Defined sort keys:
#
# 2 -- test passed
# 1 -- coverage failure
# 0 -- test really failed

function pass() {
    printf "2 %-20s pass\n" $1 >> ${summary}
}
function coverage() {
    printf "1 %-20s coverage\n" $1 >> ${summary}
}
function fail() {
    printf "0 %-20s fail\n" $1 >> ${summary}
}

summary=${outdir}/summary1
: > ${summary}
# Run the unit tests.
./test2-c |
    sed 's/^Module: \(.*\)/\1/p;d' |
    while read modname
    do
        if ./testmodule.sh ${modname} ${outdir}/${modname}
        then
            if ./checkcoverage.sh ${outdir}/${modname}
            then
                pass ${modname}
            else
                coverage ${modname}
            fi
        else
            fail ${modname}
        fi
    done

sort -n ${summary} | sed 's/^[012] //' > ${outdir}/summary

rm ${summary}

trap "" EXIT

if grep -vw pass ${outdir}/summary
then
    exit 1
else
    exit 0
fi
