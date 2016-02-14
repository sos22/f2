#! /bin/bash

set -e

testdir=$1

mkdir ${testdir}
mkdir ${testdir}/logs

make -s -j8 test2 spawnservice

function captureoutput() {
    stdbuf -o L -e L $* 2>&1
}

summary=${testdir}/summary
: > ${summary}
./test2 |
    sed 's/^Module: \(.*\)/\1/p;d' |
    while read modname
    do
        vglog=${testdir}/logs/${modname}-vg
        mkdir -p ${vglog}
        if stdbuf -o L -e L \
                  ./infra/runvalgrind.sh ${vglog}/vg.%p \
                  ./test2 "${modname}" '*' > ${testdir}/logs/${modname} 2>&1
        then
            printf "%-40s pass\n" $modname >> ${summary}
            rm -r ${vglog}
        else
            printf "%-40s fail\n" $modname >> ${summary}
        fi
    done

! grep ' fail$' ${summary}
