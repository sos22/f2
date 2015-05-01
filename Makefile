realall: all

.PHONY: buildconfig.C clean
.SUFFIXES:

# Forward most targets to the real makefile, once we've built the config file.
%: buildconfig.C
	@$(MAKE) -r -R -f Makefile2 $@

clean:
	rm -f buildconfig.C config
	make -r -R -f Makefile2 clean

config: config.gen
	@./$< $@ > $@.tmp && mv -f $@.tmp $@

buildconfig.C: config
	@./buildconfig.sh > buildconfig.C.tmp;\
	if diff -q buildconfig.C.tmp buildconfig.C 2>/dev/null;\
	then\
	   rm buildconfig.C.tmp;\
	else\
	   mv buildconfig.C.tmp buildconfig.C;\
	fi
