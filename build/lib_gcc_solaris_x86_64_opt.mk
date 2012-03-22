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
# This makefile is for building the RSTM libraries and benchmarks using
# library API, GCC, Solaris, x86_64, -O3
#
# NB: corei7 may not be available on older versions of gcc.  This makefile
#     assumes a 4.7-ish gcc.  Please adjust accordingly.
#
# Warning: This just handles platform configuration.  Everything else is
#          handled via per-folder Makefiles
#

#
# Compiler config
#
PLATFORM  = obj.lib_gcc_solaris_x86_64_opt
CXX       = g++
CXXFLAGS += -O3 -ggdb -m64 -march=corei7 -mtune=corei7 -msse2 -mfpmath=sse
LDFLAGS  += -lrt -lpthread -m64 -lmtmalloc

#
# Options to pass to STM files
#
CXXFLAGS += -DSTM_API_LIB
CXXFLAGS += -DSTM_CC_GCC
CXXFLAGS += -DSTM_OS_SOLARIS
CXXFLAGS += -DSTM_CPU_X86
CXXFLAGS += -DSTM_BITS_64
CXXFLAGS += -DSTM_OPT_O3
CXXFLAGS += -DSTM_WS_WORDLOG
