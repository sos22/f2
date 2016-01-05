#! /bin/bash

# The unit test framework automatically fails anything which crashes,
# so it can't cover crash handlers. Do some ad-hoc testing instead.

set -e
set -x
top=$(git rev-parse --show-toplevel)

make -C $top -j8 -s tests/crashhandlers/crasher

# These are expected to crash, but don't want a core dump.
ulimit -c 0

crasher=${top}/tests/crashhandlers/crasher

# Basic hello world smoke test
t=$(mktemp)
! ${crasher} crashhello 2> $t
grep -q "hello crash" $t
rm -f $t

# SEGV in one handler should prevent any others
t=$(mktemp)
! ${crasher} crashsegv 2> $t
grep -q "hello crash" $t
rm -f $t

# Disabled handlers shouldn't run.
t=$(mktemp)
! ${crasher} disablehandler 2> $t
! grep -q "hello crash" $t
rm -f $t

# A really slow crashhandler shouldn't stall shutdown forever.
start=$(date +%s)
! ${crasher} runforever
end=$(date +%s)
[ $(( $end - $start ))  -lt 5 ]

# Crash handlers bypass locks
t=$(mktemp)
! ${crasher} doublelock 2>$t
grep -q "re-locked!" $t
rm -f $t

# Low-level log messages should dump following a crash
t=$(mktemp)
! ${crasher} dumplog 2>$t
grep -q "^\+\+\+ .* debug .* this is a debug message" $t
# But not in the main log area.
! grep -q "^[0-9] .* this is a debug message" $t
rm -f $t

# One crash handler should not see changes made by the other
t=$(mktemp)
! ${crasher} changesinvisible 2>$t
# The two handlers shoudl produce the same output.
[ $(grep "^zzz 1$" $t | wc -l) -eq 2 ]
rm -f $t

# Explicit shared state should be shared.
t=$(mktemp)
! ${crasher} sharedstate 2>$t
[ $(grep "^zzz 1$" $t | wc -l) -eq 1 ]
[ $(grep "^zzz 2$" $t | wc -l) -eq 1 ]
rm -f $t

# Releasing shared state should allow us to allocate lots more.
${crasher} releaseshared

# Handlers shouldn't see mutations from worker threads
t=$(mktemp)
! ${crasher} stopotherthreads 2>$t
[ $(grep "^ZZZ [0-9]*$" $t | sort | uniq | wc -l ) -eq 1 ]
rm -f $t

echo "passed"
