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
        if stdbuf -o L -e L \
               valgrind --tool=memcheck --leak-check=full --error-exitcode=2 --trace-children=yes --trace-children-skip=/usr/bin/make \
               ./test2 "${modname}" '*' > ${testdir}/logs/${modname} 2>&1
        then
            printf "%-40s pass\n" $modname >> ${summary}
        else
            printf "%-40s fail\n" $modname >> ${summary}
        fi
    done

! grep ' fail$' ${summary}
