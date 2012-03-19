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
# The TM libraries.
#
# [mfs] Should we build .a files instead?
#

LIBDIR      = lib
LIBNAMES    = cgl norec tml cohortseager cohorts ctokenturbo ctoken   \
              llt oreceagerredo orecela orecala oreclazy oreceager
LIBOS       = $(patsubst %, $(ODIR)/%.o, $(LIBNAMES))
LIBAS       = $(patsubst %, $(ODIR)/%.a, $(LIBNAMES))

#
# .o files that are needed by all TM libraries (for now)
#

SUPTNAMES   = WBMMPolicy Common
SUPTS       = $(patsubst %, $(ODIR)/%.o, $(SUPTNAMES))

#
# .o files that are needed by orec-based TM libraries
#

ORECNAMES   = CommonOrec
ORECS       = $(patsubst %, $(ODIR)/%.o, $(ORECNAMES))

#
# .o files that are needed by lazy TM libraries
#

LAZYNAMES   = CommonLazy
LAZYS       = $(patsubst %, $(ODIR)/%.o, $(LAZYNAMES))

#
# The benchmarks
#

BENCHDIR    = bench
BENCHNAMES  = CounterBench DisjointBench DListBench ForestBench HashBench    \
              ListBench MCASBench ReadNWrite1Bench ReadWriteNBench TreeBench \
              TreeOverwriteBench TypeTest WWPathologyBench

#
# Pre-executables for benchmarks: we build a .o for each api type for each benchmark
#

APIS        = lockapi genericapi
PREBENCH    = $(foreach api,$(APIS),$(foreach bench,$(BENCHNAMES),$(ODIR)/$(bench).$(api).o))

#
# Executables: note that we are doing foreach benchmark, foreach library
#

BENCHES     = $(foreach lib,$(LIBNAMES),$(foreach bench,$(BENCHNAMES),$(ODIR)/$(bench).$(lib)))

