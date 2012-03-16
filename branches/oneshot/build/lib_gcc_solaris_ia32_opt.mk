#
#  Copyright (C) 2011
#  University of Rochester Department of Computer Science
#    and
#  Lehigh University Department of Computer Science and Engineering
# 
# License: Modified BSD
#          Please see the file LICENSE.RSTM for licensing information

#
# This makefile is for building the RSTM libraries and benchmarks using
# library API, GCC, Solaris, ia32, -O3
#
# NB: corei7 may not be available on older versions of gcc.  This makefile
# assumes a 4.7-ish gcc.  Please adjust accordingly.
#
# Warning: This won't work without also including Rules.mk, but to avoid
# weird path issues, we include it from the invocation, not from this file.
#

ODIR        = obj.lib_gcc_solaris_ia32_opt
CONFIGH     = $(ODIR)/config.h
CXX         = g++
CXXFLAGS    = -O3 -ggdb -m32 -march=corei7 -mtune=corei7 -msse2 -mfpmath=sse
CXXFLAGS   += -DSINGLE_SOURCE_BUILD -I./$(ODIR) -I./include -I./common
LDFLAGS    += -lrt -lpthread -m32 -lmtmalloc

#
# NB: WBMMPolicy isn't really a lib, but if we don't list it somewhere, it
#     gets rebuilt every time we run make
#

#
# NB: Most of the rest of this is platform-neutral and could live in its own
#     file
#

LIBDIR      = lib
LIBNAMES    = cgl norec tml cohortseager cohorts
LIBS        = $(patsubst %, $(ODIR)/%.o, $(LIBNAMES))
SUPTNAMES   = WBMMPolicy
SUPTS       = $(patsubst %, $(ODIR)/%.o, $(SUPTNAMES))
BENCHDIR    = bench
BENCHNAMES  = CounterBench DisjointBench DListBench ForestBench HashBench    \
              ListBench MCASBench ReadNWrite1Bench ReadWriteNBench TreeBench \
              TreeOverwriteBench TypeTest WWPathologyBench
BENCHES     = $(foreach lib,$(LIBNAMES),$(foreach bench,$(BENCHNAMES),$(ODIR)/$(bench).$(lib)))

#
# [mfs] TODO - we should build each benchmark 3 times, with known suffixes,
#              and then just link to create executables.  This isn't an issue
#              yet, since we only have norec and cgl, but it will be an issue
#              eventually
#
#              We also need proper dependencies
#

all: $(ODIR) $(CONFIGH) $(LIBS) $(SUPTS) $(BENCHES)
	@echo "Build complete"

$(ODIR):
	@mkdir $@

$(CONFIGH):
	@echo "// This file was auto-generated on " `date` > $@
	@echo "" >> $@
	@echo "#define STM_API_LIB" >> $@
	@echo "#define STM_CC_GCC" >> $@
	@echo "#define STM_OS_SOLARIS" >> $@
	@echo "#define STM_CPU_X86" >> $@
	@echo "#define STM_BITS_32" >> $@
	@echo "#define STM_OPT_O3" >> $@
	@echo "#define STM_WS_WORDLOG" >> $@

clean:
	@rm -rf $(ODIR)
	@echo $(ODIR) clean

#
# [mfs] ignore this; it's a playground for figuring out how to get everything
#       to build without too many build lines.  This will go away once I put
#       it in use
#

TEST  = 
TEST2 = $(ODIR)/CounterBench.cgl
testit: $(TEST2)
	@echo test=$(TEST)
	@echo $(patsubst $(ODIR)/, , $(patsubst %., , $(TEST)))
	@echo $(patsubst .%, , $(TEST))
	@echo $(notdir $(TEST2))
	@echo $(suffix $(TEST2))
	@echo $(basename $(notdir $(TEST2)))
	@echo $(subst ., , $(suffix $(TEST2)))
	@echo $(ODIR)/$(basename $(notdir $(@))).instnone
	@echo $(ODIR)/$(subst .,,$(suffix $(@))).o

