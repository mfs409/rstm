#
#  Copyright (C) 2012
#  University of Rochester Department of Computer Science
#    and
#  Lehigh University Department of Computer Science and Engineering
# 
# License: Modified BSD
#          Please see the file LICENSE.RSTM for licensing information
#

#
# Location of helper build files
#
MKFOLDER  = ../build
include $(MKFOLDER)/info.mk
include $(MKFOLDER)/algs.mk

#
# Location of RSTM libraries and includes
#
RSTMLIB = ../lib
RSTMINC = ../include

#
# Updates to flags: we want support for dependencies, and for building
# benchmarks without special .o files for the harness
#
# NB: we include $(RSTMINC) to get the header that defines the
#     instrumentation API, and we include $(RSTMLIB) to get access to the
#     atomic op definitions from platform.hpp
#
CXXFLAGS += -MMD -DSINGLE_SOURCE_BUILD -I$(RSTMINC) -I$(RSTMLIB)

#
# Output folder: Note that the folder is built at parse time, so that we
# don't have to depend on it
#
ODIR := obj.$(PLATFORM)
ifeq ($(ODIR),obj.)
else
output_folder := $(shell mkdir -p $(ODIR))
endif

#
# The names of the benchmarks.  Every benchmark will produce a .o file for
# each API that we support.
#
BENCHNAMES  = CounterBench DisjointBench DListBench ForestBench HashBench    \
              ListBench MCASBench ReadNWrite1Bench ReadWriteNBench TreeBench \
              TreeOverwriteBench TypeTest WWPathologyBench
BENCHOFILES = \
$(foreach api,$(APIS),$(foreach bench,$(BENCHNAMES),$(ODIR)/$(bench).$(api).o))
.PRECIOUS: $(BENCHOFILES)

#
# Dependencies
#
DEPS = $(patsubst %.o, %.d, $(BENCHOFILES))
-include $(DEPS)

#
# Warning: This is an uncommon approach to Makefiles
#
#   We want to be able to statically link each STM algorithm to each
#   benchmark, but we also must account for the fact that certain STM
#   algorithms have a 'preferred' instrumentation mode (i.e., not a simple
#   function call for each read/write).  The different instrumentation modes
#   correspond to the $(APIS) used for declaring $(BENCHOFILES).  Now we need
#   to make all the link rules.
#
#   The way to do this that avoids editing the makefile when more algorithms
#   are added is through a MAKEFILE template.  Below, we declare a template
#   that links executable files: when instantiated (via an $(eval $(call
#   ...)) statement that takes 3 parameters), it appends to the list of
#   executables to build, and it also creates a link rule specifically for
#   the unique executable name that is generated from the parameters.
#
define EXE_template
BENCHEXES += $(ODIR)/$(1)_$(2)_$(3)
$(ODIR)/$(1)_$(2)_$(3): $(ODIR)/$(1).$(3)api.o $(RSTMLIB)/$(ODIR)/lib$(2).a
	@echo [LD] $$@ 
	@$(CXX) -o $$@ $$^ $(LDFLAGS)
endef

#
# Executables: what to build depends on whether we are using the gcctm or not
#
ifeq ($(LIB_OVERRIDE),true)
# building for gcctm: one exe for each benchmark
BENCHEXES = $(patsubst %, $(ODIR)/%.gcctm, $(BENCHNAMES))
else
# non-gcctm build:
#   use the EXE_template to link each STMALG to BENCHNAME.stmapi.o, to link
#   each LOCKALG to BENCHNAME.lockapi.o, and to link each CUSTOMALG to
#   BENCHNAME.fptrapi.o
$(foreach bench,$(BENCHNAMES),$(foreach alg,$(STMALGS),$(eval $(call EXE_template,$(bench),$(alg),stm))))
$(foreach bench,$(BENCHNAMES),$(foreach alg,$(LOCKALGS),$(eval $(call EXE_template,$(bench),$(alg),lock))))
$(foreach bench,$(BENCHNAMES),$(foreach alg,$(CUSTOMALGS),$(eval $(call EXE_template,$(bench),$(alg),fptr))))
endif

#
# Rules for compiling individual benchmarks according to the specified API
#
$(ODIR)/%.lockapi.o: %.cpp
	@echo [CXX] $< "-->" $@ 
	@$(CXX) $(CXXFLAGS) -o $@ -c $< $(LDFLAGS) -DSTM_INST_CGL
$(ODIR)/%.stmapi.o: %.cpp
	@echo [CXX] $< "-->" $@ 
	@$(CXX) $(CXXFLAGS) -o $@ -c $< $(LDFLAGS) -DSTM_INST_STM
$(ODIR)/%.fptrapi.o: %.cpp
	@echo [CXX] $< "-->" $@ 
	@$(CXX) $(CXXFLAGS) -o $@ -c $< $(LDFLAGS) -DSTM_INST_FPTR

#
# custom rule for linking gcctm executables.
#
# NB: it suffices to use lockapi, since the API is meaningless when
#     STM_API_GCCTM is defined.
#
$(ODIR)/%.gcctm: $(ODIR)/%.lockapi.o
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)

#
# Simply typing 'make' dumps a message, rather than trying to guess a default
# platform.  benchmarks is the correct target, but isn't going to work if the
# directions are not followed
#
.default: info
benchmarks: $(BENCHEXES)
.PHONY: .default benchmarks

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
