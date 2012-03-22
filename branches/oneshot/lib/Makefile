#
#  Copyright (C) 2011
#  University of Rochester Department of Computer Science
#    and
#  Lehigh University Department of Computer Science and Engineering
# 
# License: Modified BSD
#          Please see the file LICENSE.RSTM for licensing information

#
# Pull in the 'info' target, which is the default if a platform isn't
# specified
#
MKFOLDER = ../build
include $(MKFOLDER)/info.mk

#
# Updates to flags: we want support for dependencies
#
CXXFLAGS += -MMD

#
# Output folder: we build in-tree, unless this is overridden
#
ODIR ?= obj.$(PLATFORM)

#
# The names of the STM algorithms are in a separate file, so that other
# Makefiles can include them
#
include Algs.mk

#
# Names of the supporting files that we need to build
#
COMMONNAMES = WBMMPolicy Common
ORECNAMES   = CommonOrec
LAZYNAMES   = CommonLazy

#
# Transform all source names to .o files in the output directory
#
ALGOFILES    = $(patsubst %, $(ODIR)/%.o, $(ALGNAMES))
COMMONOFILES = $(patsubst %, $(ODIR)/%.o, $(COMMONNAMES))
ORECOFILES   = $(patsubst %, $(ODIR)/%.o, $(ORECNAMES))
LAZYOFILES   = $(patsubst %, $(ODIR)/%.o, $(LAZYNAMES))

#
# The algorithm names indicate the names of the libXYZ.a files to build
#
LIBS = $(patsubst %, $(ODIR)/lib%.a, $(ALGNAMES))

#
# Dependencies when building .o files
#
DEPS = $(patsubst %, $(ODIR)/%.d, $(ALGNAMES) $(COMMONNAMES) $(ORECNAMES)    \
       $(LAZYNAMES))
-include $(DEPS)

#
# Dependencies when building .a files
#
$(ODIR)/libcgl.a:           $(ODIR)/cgl.o $(COMMONOFILES)
$(ODIR)/libnorec.a:         $(ODIR)/norec.o $(COMMONOFILES) $(LAZYOFILES)
$(ODIR)/libtml.a:           $(ODIR)/tml.o $(COMMONOFILES)
$(ODIR)/libcohortseager.a:  $(ODIR)/cohortseager.o $(COMMONOFILES) $(LAZYOFILES) $(ORECOFILES)
$(ODIR)/libcohorts.a:       $(ODIR)/cohorts.o $(COMMONOFILES) $(LAZYOFILES) $(ORECOFILES)
$(ODIR)/libctokenturbo.a:   $(ODIR)/ctokenturbo.o $(COMMONOFILES) $(LAZYOFILES) $(ORECOFILES)
$(ODIR)/libctoken.a:        $(ODIR)/ctoken.o $(COMMONOFILES) $(LAZYOFILES) $(ORECOFILES)
$(ODIR)/libllt.a:           $(ODIR)/llt.o $(COMMONOFILES) $(LAZYOFILES) $(ORECOFILES)
$(ODIR)/liboreceager.a:     $(ODIR)/oreceager.o $(COMMONOFILES) $(ORECOFILES)
$(ODIR)/liboreceagerredo.a: $(ODIR)/oreceagerredo.o $(COMMONOFILES) $(LAZYOFILES) $(ORECOFILES)
$(ODIR)/liboreclazy.a:      $(ODIR)/oreclazy.o $(COMMONOFILES) $(LAZYOFILES) $(ORECOFILES)
$(ODIR)/liborecela.a:       $(ODIR)/orecela.o $(COMMONOFILES) $(LAZYOFILES) $(ORECOFILES)
$(ODIR)/liborecala.a:       $(ODIR)/orecala.o $(COMMONOFILES) $(LAZYOFILES) $(ORECOFILES)

#
# For convenience, here are all of the .o files.  Since this is a research
# project, it's nice to keep them around when building/rebuilding
#
LIBOFILES = $(ALGOFILES) $(COMMONOFILES) $(ORECOFILES) $(LAZYOFILES)
.PRECIOUS: $(LIBOFILES)

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
	MAKEFILES="$<" $(MAKE) librstm

#
# Rule to build the output folder
#
$(ODIR):
	@mkdir $@

#
# The real target.  NB: we don't make LIBS depend on ODIR, or else they would
# be rebuilt every time the contents of ODIR change.  Instead, ODIR is the
# first thing to build.
#
librstm: $(ODIR) $(LIBS)

#
# Rule for building individual files in the lib folder
#
$(ODIR)/%.o: %.cpp
	@echo [CXX] $< "-->" $@
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

#
# Rule for making libXYZ.a files
#
$(ODIR)/%.a:
	@echo [AR] $@ 
	@$(AR) cru $@ $^
