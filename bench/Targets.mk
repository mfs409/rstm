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
# generate in order to build RSTM microbenchmarks
#

#
# The name of the directory in which we store our benchmark source codes
#

BENCHDIR    = bench

#
# The names of the benchmarks.  Every benchmark will produce a .o file for
# each API that we support.
#

BENCHNAMES  = CounterBench DisjointBench DListBench ForestBench HashBench    \
              ListBench MCASBench ReadNWrite1Bench ReadWriteNBench TreeBench \
              TreeOverwriteBench TypeTest WWPathologyBench

#
# Pre-executables for benchmarks: we build a .o for each api type for each
# benchmark
#

BENCHOFILES = $(foreach api,$(APIS),$(foreach bench,$(BENCHNAMES),$(ODIR)/$(bench).$(api).o))

#
# Executables: note that we are doing foreach benchmark, foreach STM
# algorithm
#

BENCHEXES   = $(foreach alg,$(ALGNAMES),$(foreach bench,$(BENCHNAMES),$(ODIR)/$(bench).$(alg)))

#
# Every benchmark .o generates dependencies
#

BENCHDEPS   = $(patsubst %.o, %.d, $(BENCHOFILES))
