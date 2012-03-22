#
#  Copyright (C) 2011
#  University of Rochester Department of Computer Science
#    and
#  Lehigh University Department of Computer Science and Engineering
# 
# License: Modified BSD
#          Please see the file LICENSE.RSTM for licensing information

#
# All of the platforms for which we have a common/PLATFORM.mk file are listed
# here, along with a (admittedly not so useful) build target that describes
# them all.  Including this at the top of any build target is a nice way to
# ensure that typing 'make' without a platform just spits out instructions
#

PLATFORMS = lib_gcc_linux_ia32_dbg     lib_gcc_linux_ia32_opt		\
            lib_gcc_linux_x86_64_dbg   lib_gcc_linux_x86_64_opt		\
            lib_gcc_solaris_ia32_dbg   lib_gcc_solaris_ia32_opt		\
            lib_gcc_solaris_x86_64_dbg lib_gcc_solaris_x86_64_opt

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
