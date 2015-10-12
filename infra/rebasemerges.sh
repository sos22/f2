#! /bin/bash

set -e

startat=$(git symbolic-ref --short -q HEAD)
trap "git checkout $startat" EXIT
git checkout HEAD^
git fetch arnold:f2 master:master
git branch | grep ' merge/' | while read name
                              do
                                  git checkout $name
                                  if not git rebase master
                                  then
                                      git rebase --abort
                                      continue
                                  fi
                                  git checkout master
                                  git branch -d $name || true
                              done
