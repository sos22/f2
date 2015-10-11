#! /usr/bin/env bash

# Simple script which does a kind of continuous integration, which is
# a little tiny bit like buildbot. The basic mode is that it just sits
# and runs the entire test suite over and over, generating email
# whenever anything fails. This also handles merging stuff into
# master. Whenever a merge/ branch appears, we go to merge mode. That
# means that we create a new merge-work branch, based off of master,
# and try to rebase the merge/ branch onto it one commit at a
# time. After each commit, we recompile test2-t and run any
# relevant-looking test modules (meaning anything whose coverage
# intersects the set of files modified by the commit, plus the meta
# module). At the end, we do the final check:
#
# 1) gitclean
# 2) make everything
# 3) Check that git status says we're clean
# 4) Run the entire unit test suite, including coverage checks
# 5) make clean
# 6) Check that there are no gitignored files in the repo.
#
# If everything looks good, the merge-pending branch becomes master.
# Otherwise, we generate an email describing the error, throw away
# merge-pending and the merge/ branch, and go back to normal mode.

set -x

if [ $# != 2 ]
then
    echo "need two parameters, the path to the repo and a results directory"
    exit 1
fi
repo=$(realpath $1)
results=$(realpath $2)
infradir=$(realpath $(dirname $0) )
logs=${results}/logs

mkdir -p ${results}/routine

sendemail() {
    /usr/sbin/sendmail sos22@srcf.ucam.org
}

_basemode() {
    local outdir=$1
    local commit=$2
    set -e
    git clone -q ${repo} ${outdir}/checkout
    cd ${outdir}/checkout
    git checkout $commit
    ./runtests.sh ${outdir}/testdir
}

basemode() {
    local now=$(date +%Y-%m-%d-%H-%M-%S)
    local t=${results}/routine/${now}
    local commit=$(git ls-remote ${repo} refs/heads/master | tr [:space:] ' ' | cut -d' ' -f 1)
    mkdir $t
    if ( _basemode $t $commit )
    then
        echo "${now} ${commit} PASS" >> $logs
        rm -rf $t
    else
        echo "${now} ${commit} FAIL" >> $logs
        grep -v 'pass$' ${t}/testdir/summary | sed 's/^/    /' >> $logs
        ${t}/checkout/infra/summarisefailure.sh ${t} ${now} | sendemail
    fi
}

mailheader() {
    cat <<EOF
Subject: $1
From: sos22@srcf.ucam.org
To: sos22@srcf.ucam.org

EOF
}
mergefailmsg() {
    mailheader "Merge failed: $1"
    echo "$x cannot be merged"
    cat $2
}
mergesuccmsg() {
    mailheader "Merge successful: $1"
    echo "$x successfully merged into master"
}

ltime=$(stat --printf=%Y $0)
while true
do
    git pull ${repo}
    nltime=$(stat --printf=%Y $0)
    if [ $ltime != $nltime ]
    then
        echo "re-execute $0 $*..."
        exec $*0 $*
    fi
    t=$(mktemp)
    git ls-remote ${repo} 'refs/heads/merge/*' | sed 's,refs/heads/merge/,,' > $t
    if [ $(wc -l < $t) = 0 ]
    then
        basemode
    else
        cat $t | while read commit name
        do
            t=$(mktemp)
            echo "merging $name"
            if ${infradir}/merge.sh ${results} ${repo} $name >$t 2>&1
            then
                echo "merging $name successful"
                echo "${now} MERGE $commit ($name) PASS" >> ${logs}
                mergesuccmsg $name | sendemail
            else
                echo "merging $name failed"
                echo "${now} MERGE $commit ($name) FAIL" >> ${logs}
                mergefailmsg $name $t | sendemail
            fi
            rm -f $t
            # The merge branch gets deleted even if the merge fails.
            git push --delete ${repo} merge/${name}
        done
    fi
    rm -f $t
done
