#! /bin/bash

function cescape() {
    sed 's,\\,\\\\,g;s,",\\",g;s/^/    "/;s/$/\\n"/'
}
function cescape1() {
    sed 's,\\,\\\\,g;s,",\\",g;s/^/    "/;s/$/",/'
}
function cescape2() {
    sed 's,\\,\\\\,g;s,",\\",g;s/^/    filename("/;s/$/"),/'
}

. config

cat <<EOF
/* Caution: auto-generated file.  Edit buildconfig.sh instead. */
#include "buildconfig.H"

const buildconfig
buildconfig::us(
EOF
# githead
read H < .git/HEAD
echo "    \"$H\","
# gitdiff
echo -n "    \"\""
git diff HEAD | cescape
echo "    ,"
# gitlog
echo -n "    "
git log -n 1 | cescape
echo "    ,"
# prefix
echo $PREFIX | cescape2
# cppflags
echo $CPPFLAGS | cescape1
# cxxflags
echo $CXXFLAGS | cescape1
# cflags
echo $CFLAGS | cescape1
# testing
cat <<EOF
#if TESTING
    true,
#else
    false,
#endif
EOF
# coverage
cat <<EOF
#if COVERAGE
    true
#else
    false
#endif
EOF
echo ');'
