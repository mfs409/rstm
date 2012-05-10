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
# Output folder: we build in-tree, unless this is overridden.  Note that the
# folder is built at parse time, so that we don't have to depend on its
# existance
#
ODIR := obj.$(PLATFORM)
ODIR := obj.$(PLATFORM)
ifeq ($(ODIR),obj.)
else
output_folder := $(shell mkdir -p $(ODIR))
endif

#
# Updates to flags: we want support for dependencies, and we build an .h file
#                   in the ODIR that we need to have on the include path
#
CXXFLAGS += -MMD -I$(ODIR)

#
# Names of the supporting files that we need to build
#
COMMONNAMES = WBMMPolicy Common CommonOrec tx checkpoint adaptivity \
              gcc

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
# Auto-generated header file
#
ADAPTHEADER = $(ODIR)/tmnames.autobuild.h
ADAPTLIB    = $(ODIR)/libAdapTM.a

#
# Dependencies when building .o files
#
DEPS = $(patsubst %, $(ODIR)/%.d, $(ALGNAMES) $(COMMONNAMES) $(ADAPTNAMES))
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
# The real target.  NB: we don't make LIBS depend on ODIR, or else they would
# be rebuilt every time the contents of ODIR change.  Instead, ODIR is the
# first thing to build.
#
librstm: $(ADAPTHEADER) $(LIBS) $(ADAPTLIB)

#
# Rule for building individual files in the lib folder
#
$(ODIR)/%.o: %.cpp
	@echo [CXX] $< "-->" $@
	$(_V)$(CXX) $(CXXFLAGS) -c -o $@ $<
#
# Rule for building individual files in the lib folder
#
$(ODIR)/%.o: %.S
	@echo [CCAS] $< "-->" $@
	$(_V)$(CC) $(ASFLAGS) -c -o $@ $<

#
# Rule for building .a files.  Since we use the same TX definition in all
# TMs, we have the same commonfiles, and this is very simple
#
$(ODIR)/lib%.a: $(ODIR)/%.o $(COMMONOFILES)
	@echo [AR] $@
	$(_V)$(AR) cru $@ $^

#
# Custom rule for libCGL.a---we don't want checkpoint-asm in this build because
# CGL has its own weak definition of _ITM_beginTransaction, and we never need
# to restart a transaction so we don't need checkpoint.o either (this
# eliminates a dependency on the _rstm_restore_checkpoint symbol, and means
# that libCGL can be implemnented without any asm).
#
# NB: This is _only_ ok becaue we don't support cancel at all when using CGL.
#
$(ODIR)/libCGL.a: $(ODIR)/CGL.o $(filter-out %checkpoint%.o,$(COMMONOFILES))
	@echo [AR] $@
	$(_V)$(AR) cru $@ $^


#
# Custom rule for libadaptm.a
#
$(ADAPTLIB): $(filter %AdapTM.o,$(ALGOFILES)) \
             $(filter-out %AdapTM.o,$(ALGOFILES)) $(COMMONOFILES)
	@echo [AR] $@
	$(_V)$(AR) cru $@ $^

#
# Adaptivity requires that we know the names of all TM algorithms
#
$(ADAPTHEADER): $(MKFOLDER)/algs.mk
	@echo $(ALGNAMES) TM_NAMES_MAX | sed -e 's/ /, /g' -e 's/^/enum TM_NAMES {/' -e 's/MAX/MAX};/' > $@.tmp
	@echo "namespace stm {" > $@
	@cat $@.tmp >> $@
	@echo "}" >> $@
	@rm $@.tmp
