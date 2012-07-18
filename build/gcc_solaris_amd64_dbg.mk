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
# library API, GCC, Solaris, amd64, -O0
#
# Warning: This just handles platform configuration.  Everything else is
#          handled via per-folder Makefiles
#

#
# Compiler config
#
PLATFORM  = gcc_solaris_amd64_dbg
CXX       = g++
CXXFLAGS += -O0 -ggdb -m64 -march=native -mtune=native -msse2 -mfpmath=sse
LDFLAGS  += -lrt -lpthread -m64 -lmtmalloc

#
# Options to pass to stm files
#
CXXFLAGS += -DSTM_API_LIB
CXXFLAGS += -DSTM_CC_GCC
CXXFLAGS += -DSTM_OS_SOLARIS
CXXFLAGS += -DSTM_CPU_X86
CXXFLAGS += -DSTM_BITS_64
CXXFLAGS += -DSTM_OPT_O0
CXXFLAGS += -DSTM_WS_WORDLOG
CXXFLAGS += -DSTM_PROFILETMTRIGGER_NONE
CXXFLAGS += -DSTM_TLS_GCC
CXXFLAGS += -DSTM_USE_SSE
#CXXFLAGS += -DSTM_USE_PMU
#CXXFLAGS += -DSTM_WS_BYTELOG
#CXXFLAGS += -DSTM_USE_WORD_LOGGING_VALUELIST
#CXXFLAGS += -DSTM_COUNTCONSEC_YES
#CXXFLAGS += -DSTM_PROFILETMTRIGGER_ALL
#CXXFLAGS += -DSTM_PROFILETMTRIGGER_PATHOLOGY
