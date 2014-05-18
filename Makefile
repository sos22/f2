all: realall

.SUFFIXES:

include lib.mk
include master.mk
include mastercli.mk

config: config.gen
	./$< $@ > $@.tmp && mv -f $@.tmp $@
clean::
	rm -f config

%: %.gen config
	./$< $@ > $@.tmp && mv -f $@.tmp $@

