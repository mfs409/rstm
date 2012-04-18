#
# Perform a build in the lib/ folder, and then a build in the bench/ folder,
# using the MAKEFILES envar to specify the platform.
#
# $(MAKE) will error unless the platform's corresponding definitions are in
# $(MKFOLDER)
#
%: ../build/%.mk
	@cd bayes && $(MAKE) $@
	@cd genome && $(MAKE) $@
	@cd intruder && $(MAKE) $@
	@cd kmeans && $(MAKE) $@
	@cd labyrinth && $(MAKE) $@
	@cd ssca2 && $(MAKE) $@
	@cd vacation && $(MAKE) $@
	@cd yada && $(MAKE) $@
