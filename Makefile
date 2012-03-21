#
#  Copyright (C) 2011
#  University of Rochester Department of Computer Science
#    and
#  Lehigh University Department of Computer Science and Engineering
# 
# License: Modified BSD
#          Please see the file LICENSE.RSTM for licensing information

#
# The top-level RSTM makefile simply forwards to a makefile that is specific
# to a particular build platform.  We use the MAKEFILES environment variable
# to achieve this end without significant makefile wizardry
#

PLATFORMS = lib_gcc_linux_ia32_dbg     lib_gcc_linux_ia32_opt		\
            lib_gcc_linux_x86_64_dbg   lib_gcc_linux_x86_64_opt		\
            lib_gcc_solaris_ia32_dbg   lib_gcc_solaris_ia32_opt		\
            lib_gcc_solaris_x86_64_dbg lib_gcc_solaris_x86_64_opt
OUTDIRS   = $(patsubst %, obj.%, $(PLATFORMS))

# simpy typing 'make' dumps a message, rather than trying to guess a default
info:
	@echo "You must specify your platform as the build target."
	@echo "Valid platforms are:"
	@echo "  lib_gcc_linux_ia32_dbg"
	@echo "      library API, gcc, Linux, x86, 32-bit, -O0"
	@echo "  lib_gcc_linux_ia32_opt"
	@echo "      library API, gcc, Linux, x86, 32-bit, -O3"
	@echo "  lib_gcc_linux_x86_64_dbg"
	@echo "      library API, gcc, Linux, x86, 64-bit, -O0"
	@echo "  lib_gcc_linux_x86_64_opt"
	@echo "      library API, gcc, Linux, x86, 64-bit, -O3"
	@echo "  lib_gcc_solaris_ia32_dbg"
	@echo "      library API, gcc, Solaris, x86, 32-bit, -O0"
	@echo "  lib_gcc_solaris_ia32_opt"
	@echo "      library API, gcc, Solaris, x86, 32-bit, -O0"
	@echo "  lib_gcc_solaris_x86_64_dbg"
	@echo "      library API, gcc, Solaris, x86, 64-bit, -O0"
	@echo "  lib_gcc_solaris_x86_64_opt"
	@echo "      library API, gcc, Solaris, x86, 64-bit, -O0"

# dispatch to the various platforms.  Make will error unless the platform's
# corresponding definitions are in the build folder
%: %.mk lib/Rules.mk lib/Targets.mk bench/Rules.mk bench/Targets.mk 
	MAKEFILES="lib/Targets.mk $@.mk lib/Rules.mk" $(MAKE) librstm
	MAKEFILES="lib/Targets.mk bench/Targets.mk $@.mk bench/Rules.mk" $(MAKE) benchmarks

#
# The output directory
#

$(ODIR):
	@mkdir $@

#
# Simple clean rule: kill all possible folders
#

clean:
	@rm -rf $(OUTDIRS)
