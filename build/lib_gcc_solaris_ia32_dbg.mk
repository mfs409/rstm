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
# library API, GCC, Solaris, ia32, -O0
#
# NB: corei7 may not be available on older versions of gcc.  This makefile
# assumes a 4.7-ish gcc.  Please adjust accordingly.
#

ODIR        = obj.lib_gcc_solaris_ia32_dbg
CONFIGH     = $(ODIR)/config.h
CXX         = g++
CXXFLAGS    = -O0 -ggdb -m32 -march=corei7 -mtune=corei7 -msse2 -mfpmath=sse
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
LIBNAMES    = cgl norec tml cohortseager WBMMPolicy
LIBS        = $(patsubst %, $(ODIR)/%.o, $(LIBNAMES))
BENCHDIR    = bench
BENCHNAMES  = CounterBench DisjointBench DListBench ForestBench HashBench    \
              ListBench MCASBench ReadNWrite1Bench ReadWriteNBench TreeBench \
              TreeOverwriteBench TypeTest WWPathologyBench
BENCHES     = $(patsubst %, $(ODIR)/%.cgl, $(BENCHNAMES)) \
              $(patsubst %, $(ODIR)/%.norec, $(BENCHNAMES)) \
              $(patsubst %, $(ODIR)/%.tml, $(BENCHNAMES)) \
              $(patsubst %, $(ODIR)/%.cohortseager, $(BENCHNAMES))

#
# [mfs] TODO - we should build each benchmark 3 times, with known suffixes,
#              and then just link to create executables.  This isn't an issue
#              yet, since we only have norec and cgl, but it will be an issue
#              eventually
#
#              We also need proper dependencies
#

all: $(ODIR) $(CONFIGH) $(LIBS) $(BENCHES)
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
	@echo "#define STM_OPT_O0" >> $@
	@echo "#define STM_WS_WORDLOG" >> $@

$(ODIR)/%.o: $(LIBDIR)/%.cpp $(CONFIGH)
	@echo [CXX] $< "-->" $@
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

$(ODIR)/%.cgl: $(BENCHDIR)/%.cpp $(CONFIGH) $(ODIR)/cgl.o
	@echo [CXX] $< "-->" $@ 
	@$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS) -DSTM_INST_CGL $(ODIR)/cgl.o

$(ODIR)/%.norec: $(BENCHDIR)/%.cpp $(CONFIGH) $(ODIR)/norec.o $(ODIR)/WBMMPolicy.o
	@echo [CXX] $< "-->" $@ 
	@$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS) -DSTM_INST_GENERIC $(ODIR)/norec.o $(ODIR)/WBMMPolicy.o

$(ODIR)/%.tml: $(BENCHDIR)/%.cpp $(CONFIGH) $(ODIR)/tml.o $(ODIR)/WBMMPolicy.o
	@echo [CXX] $< "-->" $@ 
	@$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS) -DSTM_INST_GENERIC $(ODIR)/tml.o $(ODIR)/WBMMPolicy.o

$(ODIR)/%.cohortseager: $(BENCHDIR)/%.cpp $(CONFIGH) $(ODIR)/cohortseager.o $(ODIR)/WBMMPolicy.o
	@echo [CXX] $< "-->" $@ 
	@$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS) -DSTM_INST_GENERIC $(ODIR)/cohortseager.o $(ODIR)/WBMMPolicy.o

clean:
	@rm -rf $(ODIR)
	@echo $(ODIR) clean

#
# [mfs] ignore this; it's a playground for figuring out how to get everything
#       to build without too many build lines.  This will go away once I put
#       it in use
#

TEST  = $(foreach lib,$(LIBNAMES),$(foreach bench,$(BENCHNAMES),$(ODIR)/$(bench).$(lib)))
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

