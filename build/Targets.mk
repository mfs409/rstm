#
#  Copyright (C) 2011
#  University of Rochester Department of Computer Science
#    and
#  Lehigh University Department of Computer Science and Engineering
# 
# License: Modified BSD
#          Please see the file LICENSE.RSTM for licensing information
#

LIBDIR      = lib
LIBNAMES    = cgl norec tml cohortseager cohorts ctokenturbo ctoken
LIBS        = $(patsubst %, $(ODIR)/%.o, $(LIBNAMES))
SUPTNAMES   = WBMMPolicy
SUPTS       = $(patsubst %, $(ODIR)/%.o, $(SUPTNAMES))
BENCHDIR    = bench
BENCHNAMES  = CounterBench DisjointBench DListBench ForestBench HashBench    \
              ListBench MCASBench ReadNWrite1Bench ReadWriteNBench TreeBench \
              TreeOverwriteBench TypeTest WWPathologyBench
BENCHES     = $(foreach lib,$(LIBNAMES),$(foreach bench,$(BENCHNAMES),$(ODIR)/$(bench).$(lib)))
