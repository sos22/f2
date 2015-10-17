#! /bin/bash

set -e

startat=$(git symbolic-ref --short -q HEAD)
trap "git checkout -q $startat 2> /dev/null" EXIT
git checkout -q HEAD^
git fetch arnold:f2 master:master
git branch | grep ' merge/' | while read name
                              do
                                  git checkout -q $name
                                  if ! git rebase master
                                  then
                                      git rebase --abort
                                      continue
                                  fi
                                  git checkout -q master
                                  git branch -d $name 2>/dev/null || true
                              done

echo

if git branch | grep -q ' merge/'
then
    echo "Merges still pending: "
    git branch | grep ' merge/' | sed 's,  merge/,,'
else
    echo "All branches merged"
fi

