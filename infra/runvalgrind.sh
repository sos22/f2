#! /bin/bash

# Run normal memcheck valgrind with all of our usual settings.
valgrind --tool=memcheck --leak-check=full --error-exitcode=2 --trace-children=yes \
         --trace-children-skip=/usr/bin/make,/bin/mktemp,/usr/bin/cmp \
         "$@"
