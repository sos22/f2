all: realall

.SUFFIXES:

include lib.mk
include master.mk
include storage.mk
include mastercli.mk
include test.mk
include tests.mk

config: config.gen
	./$< $@ > $@.tmp && mv -f $@.tmp $@
clean::
	rm -f config *.log *~ *.gcov *-c.gcda *-c.gcno

%: %.gen config
	./$< $@ > $@.tmp && mv -f $@.tmp $@

