all: realall

.SUFFIXES:

include lib.mk
include master.mk
include mastercli.mk
include test.mk

config: config.gen
	./$< $@ > $@.tmp && mv -f $@.tmp $@
clean::
	rm -f config *.log *~

%: %.gen config
	./$< $@ > $@.tmp && mv -f $@.tmp $@

