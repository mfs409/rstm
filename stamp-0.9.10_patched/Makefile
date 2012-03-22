#
#  Copyright (C) 2011
#  University of Rochester Department of Computer Science
#    and
#  Lehigh University Department of Computer Science and Engineering
# 
# License: Modified BSD
#          Please see the file LICENSE.RSTM for licensing information

#
# Location of helper build files
#
MKFOLDER = ../build
include $(MKFOLDER)/info.mk
include $(MKFOLDER)/algs.mk

#
# Output folder: we build in-tree, unless this is overridden
#
ODIR ?= obj.$(PLATFORM)

#
# Path to the stm.h file
#
STMINCLUDEDIR := ../include

#
# Path to the stm libXYZ.a files
#
LIBFOLDER = ../lib
STMLIBDIR := $(LIBFOLDER)/$(ODIR)

#
# Update CXXFLAGS with RSTM locations
#
CXXFLAGS += -Wall -I$(STMINCLUDEDIR) -DSTM -I$(LIBFOLDER) -MMD

#
# We build the program multiple times, for each STM alg.  Note that STAMP
# Rules.mk files define $(PROG)
#
TARGETS = $(foreach alg,$(ALGNAMES),$(ODIR)/$(PROG).$(alg))

#
# Switch from C to C++ compiler.  The makefiles might have updated CFLAGS
# already, so we need to append our CXXFLAGS
#
CC      = $(CXX)
CFLAGS += $(CXXFLAGS)

#
# Names of all folders we might want to clean out
#
OUTDIRS = $(patsubst %, obj.%, $(PLATFORMS))

#
# dispatch to the various platforms.  Make will error unless the platform's
# corresponding definitions are in the build folder
#
%: $(MKFOLDER)/%.mk
	MAKEFILES="$(MKFOLDER)/$@.mk bayes/Rules.mk" $(MAKE) benchmark
	MAKEFILES="$(MKFOLDER)/$@.mk genome/Rules.mk" $(MAKE) benchmark
	MAKEFILES="$(MKFOLDER)/$@.mk intruder/Rules.mk" $(MAKE) benchmark
	MAKEFILES="$(MKFOLDER)/$@.mk kmeans/Rules.mk" $(MAKE) benchmark
	MAKEFILES="$(MKFOLDER)/$@.mk labyrinth/Rules.mk" $(MAKE) benchmark
	MAKEFILES="$(MKFOLDER)/$@.mk ssca2/Rules.mk" $(MAKE) benchmark
	MAKEFILES="$(MKFOLDER)/$@.mk vacation/Rules.mk" $(MAKE) benchmark
	MAKEFILES="$(MKFOLDER)/$@.mk yada/Rules.mk" $(MAKE) benchmark

#
# Simple clean rule: kill all possible folders
#
clean:
	@rm -rf $(OUTDIRS)

#
# target to build a single STAMP benchmark
#
benchmark: $(ODIR) $(TARGETS)

#
# Make the output directory
#
$(ODIR):
	@mkdir $@

#
# Rules for building individual benchmarks according to the specified API.
# Note that the lock and stm apis are split out
#
$(ODIR)/%.$(PROG).lockapi.o: $(PROG)/%.c
	@echo [CC] $< "-->" $@ 
	@$(CC) $(CFLAGS) -o $@ -c $< -DSTM_INST_CGL
$(ODIR)/%.$(PROG).lockapi.o: lib/%.c
	@echo [CC] $< "-->" $@ 
	@$(CC) $(CFLAGS) -o $@ -c $< -DSTM_INST_CGL
$(ODIR)/%.$(PROG).genericapi.o: $(PROG)/%.c
	@echo [CC] $< "-->" $@
	@$(CC) $(CFLAGS) -o $@ -c $< -DSTM_INST_STM
$(ODIR)/%.$(PROG).genericapi.o: lib/%.c
	@echo [CC] $< "-->" $@
	@$(CC) $(CFLAGS) -o $@ -c $< -DSTM_INST_STM

#
# Rules for linking to make executables.  Unfortunately, this is a little bit
# brittle, because the rules incorporate knowledge of which APIs are required
# by whigh libraries
#
CGLFILES = $(patsubst %, $(ODIR)/%.$(PROG).lockapi.o, $(BENCHNAMES) $(LIBNAMES))
STMFILES = $(patsubst %, $(ODIR)/%.$(PROG).genericapi.o, $(BENCHNAMES) $(LIBNAMES))

#
# Dependencies.  Note that we put a dummy file in the list to prevent
# warnings when $(DEPS) is empty
#
DEPS = $(patsubst %.o, %.d, $(CGLFILES) $(STMFILES)) dummy.d
-include $(DEPS)

#
# Rules for linking to make executables.  Unfortunately, this is a little bit
# brittle, because the rules incorporate knowledge of which APIs are required
# by whigh libraries
#
$(ODIR)/$(PROG).cgl: $(CGLFILES) $(STMLIBDIR)/libcgl.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)
$(ODIR)/$(PROG).norec: $(STMFILES) $(STMLIBDIR)/libnorec.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 
$(ODIR)/$(PROG).tml: $(STMFILES) $(STMLIBDIR)/libtml.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 
$(ODIR)/$(PROG).cohortseager: $(STMFILES) $(STMLIBDIR)/libcohortseager.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 
$(ODIR)/$(PROG).cohorts: $(STMFILES) $(STMLIBDIR)/libcohorts.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 
$(ODIR)/$(PROG).ctokenturbo: $(STMFILES) $(STMLIBDIR)/libctokenturbo.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 
$(ODIR)/$(PROG).ctoken: $(STMFILES) $(STMLIBDIR)/libctoken.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 
$(ODIR)/$(PROG).llt: $(STMFILES) $(STMLIBDIR)/libllt.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 
$(ODIR)/$(PROG).oreceager: $(STMFILES) $(STMLIBDIR)/liboreceager.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 
$(ODIR)/$(PROG).oreceagerredo: $(STMFILES) $(STMLIBDIR)/liboreceagerredo.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 
$(ODIR)/$(PROG).oreclazy: $(STMFILES) $(STMLIBDIR)/liboreclazy.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 
$(ODIR)/$(PROG).orecela: $(STMFILES) $(STMLIBDIR)/liborecela.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 
$(ODIR)/$(PROG).orecala: $(STMFILES) $(STMLIBDIR)/liborecala.a
	@echo [LD] $@
	@$(CXX) -o $@ $^ $(LDFLAGS)
