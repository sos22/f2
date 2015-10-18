#! /usr/bin/env bash

set -e

results=$1
repo=$2
name=$3

now=$(date +%Y-%m-%d-%H-%M-%S)
t=${results}/merge/${name}/${now}
mkdir -p $t

expectedtestmodule() {
    local fname=$1
    local extension=$(echo $fname | sed 's/^.*\.//')
    [ $extension == "C" ] || [ $extension == "H" ] || [ $extension == "tmpl" ] || \
        [ $extension == "c" ] || [ $extension == "h" ]
}
findmodules() {
    local suffix=$1
    while read fname
    do
        if ! expectedtestmodule $fname
        then
            continue
        fi
        if echo $fname | grep -q '^tests/unit/'
        then
            echo $fname | sed 's,tests/unit/test\([0-9a-z_-]*\).C,\1,'
        else
            ./test2${suffix} findmodule $fname | grep -v ^Seed
        fi
    done
}

checkextrafiles() {
    if git status --porcelain | grep -v '^?? tmp/' | grep -q '^??'
    then
        echo "$* generated extra files"
        git status --porcelain
        exit 1
    fi
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
    echo "  $# commits left"
    git cherry-pick $commit
    make -j8 -s testall
    checkextrafiles "make testall"
    modules="meta $(git diff --name-only HEAD^ | findmodules -t | sort -u)"
    echo "test modules $modules"
    for x in $modules
    do
        # If the module was removed in this commit then we don't want
        # to test it here.
        if ./test2-t | grep -q "Module: $x\$"
        then
            ./test2-t --fuzzsched $x '*'
            checkextrafiles "test2-t"
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
checkextrafiles "make covall"
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

# No need to keep logs any more
rm -rf $t
