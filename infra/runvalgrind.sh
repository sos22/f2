#! /bin/bash

TOOL=$1
LOGFILE=$2
shift 2

case $TOOL in
    memcheck)
        tool="--tool=memcheck --leak-check=full"
        ;;
    helgrind)
        tool="--tool=helgrind --suppressions=helgrind.supp --gen-suppressions=all"
        ;;
    *)
        echo "bad tool $TOOL" >&2
        exit 1
        ;;
esac

# Run normal memcheck valgrind with all of our usual settings.
valgrind $tool --error-exitcode=2 --trace-children=yes --log-file=${LOGFILE} \
         --trace-children-skip=/usr/bin/make,/bin/mktemp,/usr/bin/cmp,/bin/dd,/bin/grep \
         "$@"
