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
# specified, and the names of the TM algorithms
#
MKFOLDER = ../build
include $(MKFOLDER)/info.mk
include $(MKFOLDER)/algs.mk

#
# Updates to flags: we want support for dependencies
#
CXXFLAGS += -MMD

#
# Output folder: we build in-tree, unless this is overridden
#
ODIR ?= obj.$(PLATFORM)

#
# Names of the supporting files that we need to build
#
COMMONNAMES = WBMMPolicy Common CommonLazy CommonOrec

#
# Transform all source names to .o files in the output directory
#
ALGOFILES    = $(patsubst %, $(ODIR)/%.o, $(ALGNAMES))
COMMONOFILES = $(patsubst %, $(ODIR)/%.o, $(COMMONNAMES))
.PRECIOUS: $(ALGOFILES) $(COMMONOFILES)

#
# The algorithm names indicate the names of the libXYZ.a files to build
#
LIBS = $(patsubst %, $(ODIR)/lib%.a, $(ALGNAMES))

#
# Dependencies when building .o files
#
DEPS = $(patsubst %, $(ODIR)/%.d, $(ALGNAMES) $(COMMONNAMES))
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
# Rule for building .a files.  Since we use the same TX definition in all
# TMs, we have the same commonfiles, and this is very simple
#
$(ODIR)/lib%.a: $(ODIR)/%.o $(COMMONOFILES)
	@echo [AR] $@
	@$(AR) cru $@ $^
