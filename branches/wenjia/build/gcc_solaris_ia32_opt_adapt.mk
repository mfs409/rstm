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
# library API, GCC, Solaris, ia32, -O3
#
# Warning: This just handles platform configuration.  Everything else is
#          handled via per-folder Makefiles
#

#
# Compiler config
#
PLATFORM  = gcc_solaris_ia32_opt_adapt
CXX       = g++
CXXFLAGS += -Wall -Wextra -Werror
CXXFLAGS += -O3 -ggdb -m32 -march=native -mtune=native -msse2 -mfpmath=sse
LDFLAGS  += -lrt -lpthread -m32 -lmtmalloc
CFLAGS   += -m32
ASFLAGS  += -m32
CC        = gcc

#
# Flag to indicate this platform should use custom ASM for checkpointing
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
CXXFLAGS += -DSTM_CPU_X86
CXXFLAGS += -DSTM_BITS_32
CXXFLAGS += -DSTM_OPT_O3
CXXFLAGS += -DSTM_WS_WORDLOG
CXXFLAGS += -DSTM_PROFILETMTRIGGER_ALL
CXXFLAGS += -DSTM_TLS_GCC
CXXFLAGS += -DSTM_USE_SSE
#CXXFLAGS += -DSTM_USE_PMU
#CXXFLAGS += -DSTM_WS_BYTELOG
#CXXFLAGS += -DSTM_USE_WORD_LOGGING_VALUELIST
#CXXFLAGS += -DSTM_COUNTCONSEC_YES
#CXXFLAGS += -DSTM_PROFILETMTRIGGER_ALL
#CXXFLAGS += -DSTM_PROFILETMTRIGGER_PATHOLOGY
