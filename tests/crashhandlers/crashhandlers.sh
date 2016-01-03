#! /bin/bash

# The unit test framework automatically fails anything which crashes,
# so it can't cover crash handlers. Do some ad-hoc testing instead.

set -e
set -x
top=$(git rev-parse --show-toplevel)

make -C $top -j8 -s tests/crashhandlers/crasher

# These are expected to crash, but don't want a core dump.
ulimit -c 0

# Basic hello world smoke test
t=$(mktemp)
! ${top}/tests/crashhandlers/crasher crashhello 2> $t
grep -q "hello crash" $t
rm -f $t

# SEGV in one handler should prevent any others
t=$(mktemp)
! ${top}/tests/crashhandlers/crasher crashsegv 2> $t
grep -q "hello crash" $t
rm -f $t

# Disabled handlers shouldn't run.
t=$(mktemp)
! ${top}/tests/crashhandlers/crasher disablehandler 2> $t
! grep -q "hello crash" $t
rm -f $t

# A really slow crashhandler shouldn't stall shutdown forever.
start=$(date +%s)
! ${top}/tests/crashhandlers/crasher runforever
end=$(date +%s)
[ $(( $end - $start ))  -lt 5 ]

echo "passed"
