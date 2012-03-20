# simpy typing 'make' dumps a message, rather than trying to guess a default
info:
	@echo "You must specify your platform as the build target."
	@echo "Valid platforms are:"
	@echo "  lib_gcc_solaris_ia32_opt"
	@echo "      library API, gcc, solaris, x86, 32-bit, -O3"
	@echo "  lib_gcc_solaris_ia32_dbg"
	@echo "      library API, gcc, solaris, x86, 32-bit, -O0"

# dispatch to the various platforms.  Make will error unless the platform's
# corresponding definitions are in the build folder
%: ../%.mk $(ODIR)
	MAKEFILES="../$@.mk bayes/Rules.mk Rules.mk" $(MAKE) default
	MAKEFILES="../$@.mk genome/Rules.mk Rules.mk" $(MAKE) default
	MAKEFILES="../$@.mk intruder/Rules.mk Rules.mk" $(MAKE) default
	MAKEFILES="../$@.mk kmeans/Rules.mk Rules.mk" $(MAKE) default
	MAKEFILES="../$@.mk labyrinth/Rules.mk Rules.mk" $(MAKE) default
	MAKEFILES="../$@.mk ssca2/Rules.mk Rules.mk" $(MAKE) default
	MAKEFILES="../$@.mk vacation/Rules.mk Rules.mk" $(MAKE) default
	MAKEFILES="../$@.mk yada/Rules.mk Rules.mk" $(MAKE) default

#
# Simple clean rule: kill all possible folders
#

clean:
	@rm -rf obj.lib_gcc_solaris_ia32_opt obj.lib_gcc_solaris_ia32_dbg
