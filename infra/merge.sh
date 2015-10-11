#! /usr/bin/env bash

set -e

results=$1
repo=$2
name=$3

now=$(date +%Y-%m-%d-%H-%M-%S)
t=${results}/merge/${name}
trap "rm -rf $t" EXIT

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

mkdir -p $t
echo "merging $name into ${t}"
git clone -b master -q ${repo} ${t}/work
cd ${t}/work
git fetch -q ${repo} merge/${name}:merge/${name}
git checkout -q master
git checkout -q -b merge-pending
# Dribble intermediate commits in one at a time until we get to the
# last one in the series.
set -e -- $(git rev-list ..merge/${name} | cut -d' ' -f 1| tr ' ' '\n' | tac | tr '\n' ' ')
echo "commits $*"
while [ $# != 1 ]
do
    commit=$1
    shift
    echo -n "merging "
    git show -s --format=oneline $commit
    git cherry-pick $commit
    rm -rf tmp
    make -j8 -s testall
    if git status --porcelain | grep -q '^??'
    then
        echo "make testall generated extra files"
        git status --porcelain
        exit 1
    fi
    modules="meta $(git diff --name-only HEAD^ | findmodules -t | sort -u)"
    echo "test modules $modules"
    for x in $modules
    do
        # If the module was removed in this commit then we don't want
        # to test it here.
        if ./test2-t | grep -q "Module: $x\$"
        then
            ./test2-t --fuzzsched $x '*'
        fi
    done
done
# The last one is special, because we run the entire suite, and
# because we perform coverage checking at the end.
echo -n "final merge "
git show -s --format=oneline $1
git cherry-pick $1
./runtests.sh ../testdir
# Check that gitignore is correct
make -j8 -s covall
if git status --porcelain | grep -v '^?? tmp/' | grep -q '^??'
then
    echo "make covall generated extra files"
    git status --porcelain
    exit 1
fi
# And check that make clean really works
make -s clean
rm -fr config tmp
if [ $(git status --porcelain --ignored | wc -l) != 0 ]
then
    echo "make clean failed to clean all files!"
    git status --porcelain --ignored
    exit 1
fi
# Merge successful. Push to master.
git push ${repo} merge-pending:master
