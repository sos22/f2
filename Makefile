all: realall

.SUFFIXES:

include cli.mk
include lib.mk
include master.mk
include storage.mk
include storageclient.mk
include test.mk
include tests.mk

# The all target only builds things which most people would want.  The
# coverage check and test suite are under a different target.
everything: realall covall testall

config: config.gen
	./$< $@ > $@.tmp && mv -f $@.tmp $@
clean::
	rm -f config *.log *~ *.gcov *-c.gcda *-c.gcno

%: %.gen config
	./$< $@ > $@.tmp && mv -f $@.tmp $@

