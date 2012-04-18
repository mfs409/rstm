#
#  Copyright (C) 2012
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

#
# Output folder: we build in-tree, unless this is overridden.  Note that the
# folder is built at parse time, so that we don't have to depend on its
# existance, but we need to handle parses of this file when a library wasn't
# given
#
ODIR := obj.$(PLATFORM)
ifeq ($(ODIR),obj.)
else
output_folder := $(shell mkdir -p $(ODIR))
endif

#
# Updates to flags: we want support for dependencies, and we build an .h file
#                   in the ODIR that we need to have on the include path
#
CXXFLAGS += -MMD -I$(ODIR) -I../include -I../

#
# All the cpp files are listed here
#
CXXNAMES = algs biteager biteagerredo bitlazy byear byeau byteeager           \
           byteeagerredo bytelazy cbr cgl cohorts cohortseager cohortsef      \
           cohortsfilter cohortslazy cohortslf cohortsli cohortsln            \
           cohortsnoorder cohortsnorec cohortsold ctoken ctokenturbo          \
           inst irrevocability llt mcs nano norec norecprio oreau orecala     \
           oreceager oreceagerredo orecela orecfair oreclazy pessimistic      \
           pipeline policies profileapp profiletm profiling ringala ringsw    \
           serial static swiss ticket tli tml tmllazy txthread types          \
           WBMMPolicy wealth fastlaneswitch fastlane1 fastlane2               \
           ctokenturboela cohortsen

#
# The name of the library to build
#
LIBNAME = $(ODIR)/librstm.a
#
# Transform all source names to .o files in the output directory
#
OFILES = $(patsubst %, $(ODIR)/%.o, $(CXXNAMES))
.PRECIOUS: $(OFILES)

#
# Dependencies
#
DEPS = $(patsubst %, $(ODIR)/%.d, $(CXXNAMES))
-include $(DEPS)

#
# Rule for building individual .o files
#
$(ODIR)/%.o: %.cpp
	@echo [CXX] $< "-->" $@
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

#
# Rule for building the library
#
$(LIBNAME): $(OFILES)
	@echo [AR] $@
	@$(AR) cru $@ $^

#
# Simply typing 'make' dumps a message, rather than trying to guess a default
# platform.  librstm is the correct target, but isn't going to work if the
# directions are not followed
#
.default: info
librstm: $(LIBNAME)
.PHONY: .default librstm

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
