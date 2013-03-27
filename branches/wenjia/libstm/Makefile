
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
# Pull in the algorithm names
#
include ./algs/algs.mk

#
# We use a perl script to autogenerate a cpp file.  This is the script name
# and the path to the perl interpreter, as well as the base name of the
# desired output file
#
ADAPTMSCRIPT = AdapTM_gen.pl
PERL = perl
ADAPTM = AdapTM

#
# Output folder: we build in-tree, unless this is overridden.  Note that the
# folder is built at parse time, so that we don't have to depend on it
# existing, but we need to handle parses of this file when a library wasn't
# given
#
ODIR := obj.$(PLATFORM)
ifeq ($(ODIR),obj.)
else
output_folder := $(shell mkdir -p $(ODIR))
endif

#
# Updates to flags: we want support for dependencies, and we build a .h file
#                   in the ODIR that we need to have on the include path
#
CXXFLAGS += -MMD -I$(ODIR)

#
# All the cpp files are listed here
#
CXXNAMES = algs cbr inst irrevocability policies profiling static txthread \
           types Toxic PMU WBMMPolicy BitLocks ByteLocks Orecs RRecs	   \
           Registration $(ADAPTM) $(ALGNAMES)

ifeq ($(USE_AOU),true)
	CXXNAMES += aou
endif

#
# Hack for AOU (for now)
#
CXXFLAGS += -I../include/

ifeq ($(CHECKPOINT),asm)
CXXNAMES += checkpoint
endif

#
# The name of the library to build
#
LIBNAME = $(ODIR)/librstm.a

#
# Build algnames-autogen.hpp in the ODIR folder
#
$(ODIR)/algnames-autogen.hpp: ./algs/algs.mk
	@echo "#ifndef ALGNAMES_AUTOGEN_HPP__" > $@
	@echo "#define ALGNAMES_AUTOGEN_HPP__" >> $@
	@echo $(ALGNAMES) ALG_MAX | sed 's/ /, /g' | sed 's/^/namespace stm{enum ALGS {/'| sed 's/ALG_MAX/ALG_MAX};}/' >> $@
	@echo "#endif // ALGNAMES_AUTOGEN_HPP__" >> $@


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
# Rules for building individual .o files
#
$(ODIR)/%.o: %.cpp
	@echo [CXX] $< "-->" $@
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

$(ODIR)/%.o: algs/%.cpp
	@echo [CXX] $< "-->" $@
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

$(ODIR)/%.o: %.S
	@echo [CC] $< "-->" $@
	@$(CC) $(ASFLAGS) -c -o $@ $<

$(ODIR)/$(ADAPTM).cpp: ./algs/algs.mk $(ADAPTMSCRIPT)
	@echo [PERL] $@
	@$(PERL) $(ADAPTMSCRIPT) $(ALGNAMES) > $@

$(ODIR)/%.o: $(ODIR)/%.cpp
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
librstm: $(ODIR)/algnames-autogen.hpp $(LIBNAME)
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
