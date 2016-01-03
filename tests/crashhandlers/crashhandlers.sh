#! /bin/bash

# The unit test framework automatically fails anything which crashes,
# so it can't cover crash handlers. Do some ad-hoc testing instead.

set -e
set -x
top=$(git rev-parse --show-toplevel)

make -C $top -j8 -s tests/crashhandlers/crasher

# These are expected to crash, but don't want a core dump.
ulimit -c 0

t=$(mktemp)
! ${top}/tests/crashhandlers/crasher crashhello 2> $t
grep -q "hello crash" $t
rm -f $t

echo "passed"
