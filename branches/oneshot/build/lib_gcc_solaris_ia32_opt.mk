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

ODIR        = obj.lib_gcc_solaris_ia32_opt
CONFIGH     = $(ODIR)/config.h
CXX         = g++
CXXFLAGS    = -O3 -ggdb -m32 -march=corei7 -mtune=corei7 -msse2 -mfpmath=sse
CXXFLAGS   += -DSINGLE_SOURCE_BUILD -I./$(ODIR) -I./include -I./common
LDFLAGS    += -lrt -lpthread -m32 -lmtmalloc
LIBDIR      = lib
LIBS        = $(ODIR)/cgl.o
BENCHDIR    = bench
BENCHFILES  = CounterBench DisjointBench DListBench ForestBench HashBench    \
              ListBench MCASBench ReadNWrite1Bench ReadWriteNBench TreeBench \
              TreeOverwriteBench TypeTest WWPathologyBench
BENCHES     = $(patsubst %, $(ODIR)/%.cgl, $(BENCHFILES))

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
	@echo "#define STM_OPT_O3" >> $@

$(ODIR)/%.o: $(LIBDIR)/%.cpp $(CONFIGH)
	@echo [CXX] $< "-->" $@
	@$(CXX) $(CXXFLAGS) -c -o $@ $< $(LDFLAGS)

$(ODIR)/%.cgl: $(BENCHDIR)/%.cpp $(CONFIGH) $(ODIR)/cgl.o
	@echo [CXX] $< "-->" $@ 
	@$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS) -DSTM_INST_CGL $(ODIR)/cgl.o
