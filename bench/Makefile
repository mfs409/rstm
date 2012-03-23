#
#  Copyright (C) 2011
#  University of Rochester Department of Computer Science
#    and
#  Lehigh University Department of Computer Science and Engineering
# 
# License: Modified BSD
#          Please see the file LICENSE.RSTM for licensing information
#

#
# Pull in the 'info' target, which is the default if a platform isn't
# specified
#
MKFOLDER  = ../build
include $(MKFOLDER)/info.mk
include $(MKFOLDER)/algs.mk

#
# Location of RSTM
#
LIBFOLDER = ../lib

#
# Updates to flags: we want support for dependencies, and for building
# benchmarks without intermediate .o files
#
CXXFLAGS += -MMD -DSINGLE_SOURCE_BUILD -I../include -I$(LIBFOLDER)

#
# Output folder: we build in-tree, unless this is overridden
#
ODIR ?= obj.$(PLATFORM)

#
# The names of the benchmarks.  Every benchmark will produce a .o file for
# each API that we support.
#
BENCHNAMES = CounterBench DisjointBench DListBench ForestBench HashBench    \
             ListBench MCASBench ReadNWrite1Bench ReadWriteNBench TreeBench \
             TreeOverwriteBench TypeTest WWPathologyBench

#
# Pre-executables for benchmarks: we build a .o for each api type for each
# benchmark
#
BENCHOFILES = $(foreach api,$(APIS),$(foreach bench,$(BENCHNAMES),$(ODIR)/$(bench).$(api).o))
.PRECIOUS: $(BENCHOFILES)

#
# Executables: note that we are doing foreach benchmark, foreach STM
# algorithm
#
BENCHEXES = $(foreach alg,$(ALGNAMES),$(foreach	bench,$(BENCHNAMES),$(ODIR)/$(bench).$(alg)))

#
# Dependencies
#
DEPS = $(patsubst %.o, %.d, $(BENCHOFILES))
-include $(DEPS)

#
# simply typing 'make' dumps a message, rather than trying to guess a default
# platform
#
default: info

#
# In order to avoid creating config files, we use the following approach:
# when we make, we give the name of a platform being targeted.  If that
# platform exists, then there's a file in $(MKFOLDER) with the platform as
# its basename and .mk as its suffix.  If we re-invoke make including that
# platform as an additional makefile (to this one), then we'll have all the
# definitions we need to build the right way.
#
%: $(MKFOLDER)/%.mk
	MAKEFILES="$<" $(MAKE) benchmarks

#
# Rule to build the output folder
#
$(ODIR):
	@mkdir $@

#
# The real target.  NB: we don't make BENCHEXES depend on ODIR, or else they
# would be rebuilt every time the contents of ODIR change.  Instead, ODIR is
# the first thing to build.
#
benchmarks: $(ODIR) $(BENCHEXES)

#
# Rules for building individual benchmarks according to the specified API
#
$(ODIR)/%.lockapi.o: %.cpp
	@echo [CXX] $< "-->" $@ 
	@$(CXX) $(CXXFLAGS) -o $@ -c $< $(LDFLAGS) -DSTM_INST_CGL
$(ODIR)/%.genericapi.o: %.cpp
	@echo [CXX] $< "-->" $@ 
	@$(CXX) $(CXXFLAGS) -o $@ -c $< $(LDFLAGS) -DSTM_INST_STM

#
# Rules for linking to make executables.  Unfortunately, this is a little bit
# brittle, because the rules incorporate knowledge of which APIs are required
# by whigh libraries
#
$(ODIR)/%.cgl: $(ODIR)/%.lockapi.o $(LIBFOLDER)/$(ODIR)/libcgl.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)
$(ODIR)/%.norec: $(ODIR)/%.genericapi.o $(LIBFOLDER)/$(ODIR)/libnorec.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)
$(ODIR)/%.tml: $(ODIR)/%.genericapi.o $(LIBFOLDER)/$(ODIR)/libtml.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)
$(ODIR)/%.cohortseager: $(ODIR)/%.genericapi.o $(LIBFOLDER)/$(ODIR)/libcohortseager.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)
$(ODIR)/%.cohorts: $(ODIR)/%.genericapi.o $(LIBFOLDER)/$(ODIR)/libcohorts.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)
$(ODIR)/%.ctokenturbo: $(ODIR)/%.genericapi.o $(LIBFOLDER)/$(ODIR)/libctokenturbo.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)
$(ODIR)/%.ctoken: $(ODIR)/%.genericapi.o $(LIBFOLDER)/$(ODIR)/libctoken.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)
$(ODIR)/%.llt: $(ODIR)/%.genericapi.o $(LIBFOLDER)/$(ODIR)/libllt.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)
$(ODIR)/%.oreceager: $(ODIR)/%.genericapi.o $(LIBFOLDER)/$(ODIR)/liboreceager.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)
$(ODIR)/%.oreceagerredo: $(ODIR)/%.genericapi.o $(LIBFOLDER)/$(ODIR)/liboreceagerredo.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)
$(ODIR)/%.oreclazy: $(ODIR)/%.genericapi.o $(LIBFOLDER)/$(ODIR)/liboreclazy.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)
$(ODIR)/%.orecela: $(ODIR)/%.genericapi.o $(LIBFOLDER)/$(ODIR)/liborecela.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)
$(ODIR)/%.orecala: $(ODIR)/%.genericapi.o $(LIBFOLDER)/$(ODIR)/liborecala.a
	@echo [LD] $@
	@$(CXX) -o $@ $^ $(LDFLAGS)
$(ODIR)/%.oreclazybackoff: $(ODIR)/%.genericapi.o $(LIBFOLDER)/$(ODIR)/liboreclazybackoff.a
	@echo [LD] $@
	@$(CXX) -o $@ $^ $(LDFLAGS)
$(ODIR)/%.oreclazyhb: $(ODIR)/%.genericapi.o $(LIBFOLDER)/$(ODIR)/liboreclazyhb.a
	@echo [LD] $@
	@$(CXX) -o $@ $^ $(LDFLAGS)
$(ODIR)/%.oreclazyhour: $(ODIR)/%.genericapi.o $(LIBFOLDER)/$(ODIR)/liboreclazyhour.a
	@echo [LD] $@
	@$(CXX) -o $@ $^ $(LDFLAGS)
