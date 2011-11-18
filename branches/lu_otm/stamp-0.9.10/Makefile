# ensure we have the 'bins' directory
BITS=$(shell grep BITS ../obj.x86/stm/config.h | sed 's/.*_//')
EXEFOLDER=$(patsubst %, bins%, $(BITS))

.PHONY: all clean realclean

all: $(EXEFOLDER)
	@cd bayes && $(MAKE)
	@cd genome && $(MAKE)
	@cd intruder && $(MAKE)
	@cd kmeans && $(MAKE)
	@cd labyrinth && $(MAKE)
	@cd ssca2 && $(MAKE)
	@cd vacation && $(MAKE)
	@cd yada && $(MAKE)
	@cp bayes/obj/bayes genome/obj/genome intruder/obj/intruder   \
            kmeans/obj/kmeans labyrinth/obj/labyrinth ssca2/obj/ssca2 \
            vacation/obj/vacation yada/obj/yada                       \
            $(EXEFOLDER)/

clean:
	@cd bayes && $(MAKE) $@
	@cd genome && $(MAKE) $@
	@cd intruder && $(MAKE) $@
	@cd kmeans && $(MAKE) $@
	@cd labyrinth && $(MAKE) $@
	@cd ssca2 && $(MAKE) $@
	@cd vacation && $(MAKE) $@
	@cd yada && $(MAKE) $@

realclean:
	@cd bayes && $(MAKE) $@
	@cd genome && $(MAKE) $@
	@cd intruder && $(MAKE) $@
	@cd kmeans && $(MAKE) $@
	@cd labyrinth && $(MAKE) $@
	@cd ssca2 && $(MAKE) $@
	@cd vacation && $(MAKE) $@
	@cd yada && $(MAKE) $@
	@rm -rf bins*

$(EXEFOLDER):
	mkdir $(EXEFOLDER)
