#! /bin/bash

set -e

# Run all of the defined test modules, recording their results.  Fail
# if any of them fail (including coverage failures)

outdir=$1
mkdir ${outdir}
trap "rm -rf ${outdir}" EXIT

summary=${outdir}/summary1
: > ${summary}
./test2-c |
    sed 's/^Module: \(.*\)/\1/p;d' |
    while read modname
    do
        if ./testmodule.sh ${modname} ${outdir}/${modname}
        then
            if ./checkcoverage.sh ${outdir}/${modname}
            then
                printf "2 %-20s pass\n" ${modname} >> ${summary}
            else
                printf "1 %-20s coverage\n" ${modname} >> ${summary}
            fi
        else
            printf "0 %-20s fail\n" ${modname} >> ${summary}
        fi
    done

sort -n ${summary} |
    sed 's/^[012] //' > ${outdir}/summary

rm ${outdir}/summary1

trap "" EXIT

if grep -vw pass ${outdir}/summary
then
    exit 1
else
    exit 0
fi
