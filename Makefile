all: realall

-include config

.SUFFIXES:

include binaries.mk
include jobexec.mk
include lib.mk
include spawnservice.mk
include test2.mk
include tests.mk
include tests/mk

# The all target only builds things which most people would want.  The
# coverage check and test suite are under a different target.
everything: realall covall testall profall

clean::
	find . -name '*.log' -o -name '*~' -o -name '*.gcov' -o -name   \
		'*-c.gcda' -o -name '*-c.gcno'                        | \
		xargs -r rm

%: %.gen config
	@./$< $@ > $@.tmp && mv -f $@.tmp $@

gitversion.H: _does_not_exist_
	@./mkgitversion
clean::
	rm -f gitversion.H config.H

# Specialise the .gen rule for config so that it doesn't induce a
# circular dependency.
config: config.gen
	@./$< $@ > $@.tmp && mv -f $@.tmp $@

# Special target which is sometimes useful for forcing something to
# get rebuilt every time.  Mostly useful for makefile fragments,
# because if one of them is a direct dependency of .PHONY then you get
# an infinite loop.
.PHONY: _does_not_exist_
_does_not_exist_:
	@(! [ -e _does_not_exist_ ])
