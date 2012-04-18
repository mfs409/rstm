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
CXXFLAGS += -MMD -DSINGLE_SOURCE_BUILD -I$(RSTMINC) -I$(RSTMLIB) -I../

#
# updates to LDFLAGS
#
LDFLAGS += -L../libstm/$(ODIR)/ -lrstm

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
BENCHOFILES = $(patsubst %, $(ODIR)/%.o, $(BENCHNAMES))
.PRECIOUS: $(BENCHOFILES)
BENCHEXES = $(patsubst %, $(ODIR)/%, $(BENCHNAMES))

#
# Dependencies
#
DEPS = $(patsubst %.o, %.d, $(BENCHOFILES))
-include $(DEPS)

#
# Link rule
#
$(ODIR)/%: $(ODIR)/%.o ../libstm/$(ODIR)/librstm.a
	@echo [LD] $@
	@$(CXX) -o $@ $^ $(LDFLAGS)

#
# Rules for compiling .o files from .cpp files
#
$(ODIR)/%.o: %.cpp
	@echo [CXX] $< "-->" $@ 
	@$(CXX) $(CXXFLAGS) -o $@ -c $<

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
