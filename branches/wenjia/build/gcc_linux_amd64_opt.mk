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
# library API, GCC, Linux, amd64, -O3
#
# Warning: This just handles platform configuration.  Everything else is
#          handled via per-folder Makefiles
#

#
# Compiler config
#
PLATFORM  = gcc_linux_amd64_opt
CXX       = g++
CXXFLAGS += -Wall -Wextra -Werror
CXXFLAGS += -O3 -ggdb -m64 -march=native -mtune=native -msse2 -mfpmath=sse
LDFLAGS  += -lrt -lpthread -m64
CFLAGS   += -m64
ASFLAGS  += -m64
CC        = gcc

#
# Options to pass to STM files
#
CXXFLAGS += -DSTM_API_LIB
CXXFLAGS += -DSTM_CC_GCC
CXXFLAGS += -DSTM_OS_LINUX
CXXFLAGS += -DSTM_CPU_X86
CXXFLAGS += -DSTM_BITS_64
CXXFLAGS += -DSTM_OPT_O3
CXXFLAGS += -DSTM_WS_WORDLOG
CXXFLAGS += -DSTM_PROFILETMTRIGGER_NONE
CXXFLAGS += -DSTM_USE_SSE
#CXXFLAGS += -DSTM_USE_PMU
#CXXFLAGS += -DSTM_WS_BYTELOG
#CXXFLAGS += -DSTM_USE_WORD_LOGGING_VALUELIST
#CXXFLAGS += -DSTM_COUNTCONSEC_YES
#CXXFLAGS += -DSTM_PROFILETMTRIGGER_ALL
#CXXFLAGS += -DSTM_PROFILETMTRIGGER_PATHOLOGY
CXXFLAGS += -DSTM_INST_FINEGRAINADAPT
CXXFLAGS += -DSTM_API_TLSPARAM
