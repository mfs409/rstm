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
# library API, GCC, Solaris, sparc (64-bit), -O3
#
# Warning: This just handles platform configuration.  Everything else is
#          handled via per-folder Makefiles
#

#
# Compiler config
#
PLATFORM  = gcc_solaris_sparc64_opt
CXX       = g++
CXXFLAGS += -O3 -ggdb -m64 -native
LDFLAGS  += -lrt -lpthread -m64 -lmtmalloc
CFLAGS   += -m64
ASFLAGS  += -m64 -DSTM_BITS_64 -DSTM_CPU_SPARC
CC        = gcc

#
# Flag to indicate that this platform should use custom ASM for checkpointing
#
# NB: this will go away once all platforms use custom ASM
#
CHECKPOINT = asm
CXXFLAGS += -DSTM_CHECKPOINT_ASM

#
# Options to pass to STM files
#
CXXFLAGS += -DSTM_API_LIB
CXXFLAGS += -DSTM_CC_GCC
CXXFLAGS += -DSTM_OS_SOLARIS
CXXFLAGS += -DSTM_CPU_SPARC
CXXFLAGS += -DSTM_BITS_64
CXXFLAGS += -DSTM_OPT_O3
CXXFLAGS += -DSTM_WS_WORDLOG
CXXFLAGS += -DSTM_PROFILETMTRIGGER_NONE
CXXFLAGS += -DSTM_TLS_GCC
#CXXFLAGS += -DSTM_USE_PMU
#CXXFLAGS += -DSTM_WS_BYTELOG
#CXXFLAGS += -DSTM_USE_WORD_LOGGING_VALUELIST
#CXXFLAGS += -DSTM_COUNTCONSEC_YES
#CXXFLAGS += -DSTM_PROFILETMTRIGGER_ALL
#CXXFLAGS += -DSTM_PROFILETMTRIGGER_PATHOLOGY
