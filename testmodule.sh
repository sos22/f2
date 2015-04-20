#! /bin/bash

set -e

: ${TMPDIR:=/tmp}
module=${1}
outfile=${2}
base=$(pwd)
t=$(mktemp -d ${TMPDIR}/testcov.XXXXXX)
t2=$(mktemp -d ${TMPDIR}/testcov.report.XXXXXX)
filelist=$(mktemp)
trap "rm -rf $t $t2 ${filelist}" EXIT
# Run the tests
GCOV_PREFIX_STRIP=$(($(pwd | sed 's,[^/],,g' | wc -c) - 1))  \
    GCOV_PREFIX=${t}                                         \
    ${base}/test2-c "$module" "*"

# Set up symlinks so that gcov can find source files.
find ${base} -type f \( -name '*.[CHch]' -o -name '*.tmpl' \) |
    sed "s,^${base},," |
    while read fname
    do
        in=${base}/${fname}
        mkdir -p ${t}/$(dirname ${fname}) $(dirname ${t2}/${fname})
        ln -s ${in} ${t2}/${fname}
    done
find ${base} -type f -name '*.gcno' |
    sed "s,^${base},," |
    while read fname
    do
        in=${base}/${fname}
        b=$(basename ${fname})
        ln -s ${in} ${t}/${b}
        ln -s ${in} ${t2}/${b}
    done
find ${t} -type f -name '*.gcda' |
    while read fname
    do
        b=$(basename $fname)
        if ! [ -r "${t}/${b}" ]
        then
            ln -s ${fname} ${t}/${b}
        fi
        ln -s ${fname} ${t2}/${b}
    done
# Extract coverage numbers
cd ${t2}
gcov --no-output -o ${t} -s ${base} -b -c $(find ${t} -type f -name '*.gcda') > gcov_output 2>/dev/null
cd - > /dev/null

./test2-c --stat "$1" | sed 's/^[[:space:]]*File:[[:space:]]*\(.*\)$/\1/p;d' > $filelist
eval $(awk "/^File / { filename=\$2; } /Taken at least once:/ { print filename \" \" \$4 \" \" \$6; }" < ${t2}/gcov_output |
              sed "s/'\([a-zA-Z0-9./]*\)' once:\([0-9.]*\)% \([0-9]*\)/\1 \2 \3/p;d" |
              ( nrbranches=0
                takenbranches=0
                while read name perc tot
                do
                    if ! grep -q "$name" $filelist
                    then
                        continue
                    fi
                    echo "Branch: name $name perc $perc tot $tot" >&2
                    nrbranches=$(echo "$nrbranches + $tot" | bc)
                    takenbranches=$(echo "$takenbranches + $tot * $perc" | bc)
                done
                echo nrbranches=${nrbranches} takenbranches=${takenbranches} ) )
eval $(awk "/^File / { filename=\$2; } /Lines executed:/ { print filename \" \" \$2 \" \" \$4; }" < ${t2}/gcov_output |
              sed "s/'\([a-zA-Z0-9./]*\)' executed:\([0-9.]*\)% \([0-9]*\)/\1 \2 \3/p;d" |
              ( nrlines=0
                execlines=0
                while read name perc tot
                do
                    if ! grep -q "$name" $filelist
                    then
                        continue
                    fi
                    echo "Lines: name $name perc $perc tot $tot" >&2
                    nrlines=$(echo "$nrlines + $tot" | bc)
                    execlines=$(echo "$execlines + $tot * $perc" | bc)
                done
                echo nrlines=${nrlines} execlines=${execlines} ) )

echo nrbranches=$nrbranches takenbranches=$takenbranches nrlines=$nrlines execlines=$execlines
linecov=$(echo "scale=4; ${execlines} / $nrlines" | bc)
branchcov=$(echo "scale=4; ${takenbranches} / $nrbranches" | bc)

# Build the final report.
echo branchcoverage=${branchcov} linecoverage=${linecov} > ${outfile}
echo ------ >> ${outfile}
cd ${t2}
gcov -o ${t} -s ${base} $(find ${t} -type f -name '*.gcda') > /dev/null
cd - > /dev/null
cat ${filelist} | while read fname
do
    cat ${t2}/${fname}.gcov 2>/dev/null || true
done >> ${outfile}