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

if [ $# != 2 ]
then
    echo "need two parameters, the path to the repo and a results directory"
    exit 1
fi
repo=$(realpath $1)
results=$(realpath $2)

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
    local logs=${results}/logs
    local commit=$(git -C ${repo} rev-parse HEAD)
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

findmodules() {
    local suffix=$1
    while read fname
    do
        if echo $fname | grep -q '^tests/unit/'
        then
            echo $fname | sed 's,tests/unit/test\([0-9a-z_-]*\).C,\1,'
        else
            ./test2${suffix} findmodule $fname | grep -v ^Seed
        fi
    done
}

merge() {
    set -e
    local name=$1
    local now=$(date +%Y-%m-%d-%H-%M-%S)
    local t=${results}/merge/${name}/${now}
    trap "rm -rf $t" EXIT
    mkdir -p $t
    echo "merging $name into ${t}"
    git clone -q ${repo} ${t}/work
    cd ${t}/work
    git fetch -q ${repo} merge/${name}:merge/${name}
    git checkout -q master
    git checkout -q -b merge-pending
    # Dribble intermediate commits in one at a time until we get to
    # the last one in the series.
    set $(git rev-list ..merge/${name} | tr ' ' '\n' | tac | tr '\n' ' ')
    echo "commits $*"
    while [ $# != 1 ]
    do
        local commit=$1
        shift
        echo -n "merging "
        git show -s --format=oneline $commit
        git cherry-pick $commit
        make -j8 -s testall
        if git status --porcelain | grep -q '^??'
        then
            echo "make testall generated extra files"
            git status --porcelain
            exit 1
        fi
        local modules="meta $(git diff --name-only HEAD^ | findmodules -t | sort -u)"
        echo "test modules $modules"
        for x in $modules
        do
            ./test2-t --fuzzsched $x '*'
        done
    done
    # The last one is special, because we run the entire suite, and
    # because we perform coverage checking at the end.
    echo -n "final merge "
    git show -s --format=oneline $1
    git cherry-pick $1
    ./runtests.sh testdir
    # Check that gitignore is correct
    make -j8 -s covall
    if git status --porcelain | grep -q '^??'
    then
        echo "make covall generated extra files"
        git status --porcelain
        exit 1
    fi
    # And check that make clean really works
    make -s clean
    rm -f config
    if [ $(git status --porcelain --ignored | grep -vw testdir | wc -l) != 0 ]
    then
        echo "make clean failed to clean all files!"
        git status --porcelain --ignored
        exit 1
    fi
    # Merge successful. Push to master.
    git push ${repo} merge-pending:master
    exit 0
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

while true
do
    merges=
    shopt -s nullglob
    for x in ${repo}/refs/heads/merge/*
    do
        merges="$merges $(echo $x | sed 's,^.*/,,')"
    done
    if [ "$merges" = "" ]
    then
        basemode
    else
        for x in $merges
        do
            t=$(mktemp)
            echo "merging $x"
            if ( merge $x ) >$t 2>&1
            then
                echo "merging $x successful"
                mergesuccmsg $x | sendemail
            else
                echo "merging $x failed"
                mergefailmsg $x $t | sendemail
            fi
            rm -f $t
            # The merge branch gets deleted even if the merge fails.
            git push --delete ${repo} merge/${x}
        done
    fi
done
