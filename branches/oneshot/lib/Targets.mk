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
# This makefile specifies and organizes the names of the files we must
# generate in order to build RSTM libraries
#

#
# The name of the directory in which we store our STM source codes
#

LIBDIR       = lib

#
# The names of the STM algorithms.  Every algorithm produces a .o file
#

ALGNAMES     = cgl norec tml cohortseager cohorts ctokenturbo ctoken   \
               llt oreceagerredo orecela orecala oreclazy oreceager
ALGOFILES    = $(patsubst %, $(ODIR)/%.o, $(ALGNAMES))

#
# Applications will depend on this .mk file to know what needs to be built.
# Thus we must declare the different APIs here
#

APIS         = lockapi genericapi

#
# .o files that are needed by all TM libraries (for now)
#

COMMONNAMES  = WBMMPolicy Common
COMMONOFILES = $(patsubst %, $(ODIR)/%.o, $(COMMONNAMES))

#
# .o files that are needed by orec-based TM libraries
#

ORECNAMES    = CommonOrec
ORECOFILES   = $(patsubst %, $(ODIR)/%.o, $(ORECNAMES))

#
# .o files that are needed by lazy TM libraries
#

LAZYNAMES    = CommonLazy
LAZYOFILES   = $(patsubst %, $(ODIR)/%.o, $(LAZYNAMES))

#
# The algorithm names indicate the names of the libXYZ.a files to build
#

LIBS         = $(patsubst %, $(ODIR)/lib%.a, $(ALGNAMES))

#
# Every .o file generates dependencies
#

LIBDEPS      = $(patsubst %, $(ODIR)/%.d, $(ALGNAMES) $(COMMONNAMES) $(ORECNAMES) $(LAZYNAMES))

#
# For convenience, here are all of the .o files
#
LIBOFILES    = $(ALGOFILES) $(COMMONOFILES) $(ORECOFILES) $(LAZYOFILES)
