#! /bin/bash

set -e

# Run all of the defined test modules, recording their results.  Fail
# if any of them fail (including coverage failures)

outdir=$1
mkdir ${outdir}
mkdir ${outdir}/logs
trap "rm -rf ${outdir}" EXIT

make -s -j8 test2-c

# summary1 has one line for every individual test.  The first field of
# the line is a sort key and the rest is the message itself.

# Defined sort keys:
#
# 3 -- test passed
# 2 -- coverage failure
# 1 -- test really failed
# 0 -- header

function pass() {
    printf "3 %-40s pass\n" "$1" >> ${summary}
}
function coverage() {
    printf "2 %-40s coverage\n" "$1" >> ${summary}
}
function fail() {
    printf "1 %-40s fail\n" "$1" >> ${summary}
}
function header() {
    printf "0 %-40s meta\n" "$1" >> ${summary}
}

function captureoutput() {
    stdbuf -o L -e L $* 2>&1
}

function testmodule() {
    local modname=$1
    if captureoutput ./testmodule.sh ${modname} ${outdir}/${modname} > ${outdir}/logs/${modname}
    then
        rm ${outdir}/logs/${modname}
    else
        false
    fi
}

summary=${outdir}/summary1
: > ${summary}
sha=$(git rev-parse HEAD)
header "testing $sha"
# Run the unit tests.
./test2-c |
    sed 's/^Module: \(.*\)/\1/p;d' |
    while read modname
    do
        if testmodule ${modname}
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

sort -n ${summary} | sed 's/^[0-9]* //' > ${outdir}/summary

rm ${summary}

trap "" EXIT

if grep -vw '\(pass\)\|\(meta\)' ${outdir}/summary
then
    exit 1
else
    exit 0
fi
