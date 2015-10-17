#! /bin/bash

set -e

startat=$(git symbolic-ref --short -q HEAD)
trap "git checkout -q $startat" EXIT
git checkout -q HEAD^
git fetch arnold:f2 master:master
git branch | grep ' merge/' | while read name
                              do
                                  git checkout -q $name
                                  git rebase master
                                  git checkout -q master
                                  git branch -d $name 2>/dev/null && continue
                                  git push arnold:f2 ${name}:${name}
                              done
