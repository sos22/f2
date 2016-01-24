#! /bin/bash

# Take one of the test directories produced by testmodule.sh and spit
# out a summary of what went wrong.

if [ $# -ne 2 ]
then
    echo "need two arguments, test dir and timestamp"
    exit 1
fi

builddir=$1/checkout
testdir=$1/testdir
when=$2
mailaddr=$3

if ! [ -f ${testdir}/summary ]
then
    echo "$testdir has no summary file?"
    exit 1
fi
if ! [ -f ${builddir}/checkcoverage.sh ]
then
    echo "${builddir} has no checkcoverage.sh?"
    exit 1
fi

# Summary line.
fail=$(mktemp)
coverage=$(mktemp)
grep 'fail$' ${testdir}/summary | sed 's/[[:space:]]*fail$//' > ${fail}
grep 'coverage$' ${testdir}/summary | sed 's/[[:space:]]*coverage$//'  > ${coverage}
nrfail=$(wc -l < ${fail})
nrcoverage=$(wc -l < ${coverage})

header() {
    local subject="$*"
    echo "Subject: $when: $subject"
    echo "From: $mailaddr"
    echo "To: $mailaddr"
    echo
    grep 'meta$' ${testdir}/summary | sed 's/[[:space:]]*meta$//'
    echo
}
coveragesummary() {
    cat $coverage | while read cov
                    do
                        ${builddir}/checkcoverage.sh ${testdir}/$cov
                    done
}
if [ $nrfail -eq 0 ]
then
    rm -f $fail
    if [ $nrcoverage -eq 0 ]
    then
        header "PASS"
        rm -f $coverage
        exit 0
    fi
    if [ $nrcoverage -lt 5 ]
    then
        header "COVERAGE $(tr '\n' ' ' < ${coverage})"
    else
        header "COVERAGE {$nrcoverage failures}"
    fi
    coveragesummary
    rm -f $coverage
    exit 0
fi

if [ $nrcoverage -eq 0 ]
then
    covbit=""
else
    covbit=" (plus $nrcoverage coverage)"
fi

if [ $nrfail -lt 5 ]
then
    header "FAIL $(tr '\n' ' ' < ${fail})${covbit}"
else
    header "FAIL {$nrfail failures}${covbit}"
fi

cat $fail | while read fail
            do
                if [ $nrfail -ne 1 ]
                then
                    echo "---------- ${fail} -----------"
                fi
                cat ${testdir}/logs/${fail}
                echo "BACKTRACES:"
                ./infra/backtrace.sh ${testdir}/logs/${fail}
                echo
            done

if [ $nrcoverage -ne 0 ]
then
    echo
    echo "=============== coverage ============="
    coveragesummary
fi

rm -f $fail
rm -f $coverage
