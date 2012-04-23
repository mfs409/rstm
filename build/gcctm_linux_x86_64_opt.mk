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
# library API, GCC, Linux, x86_64, -O3
#
# Warning: This just handles platform configuration.  Everything else is
#          handled via per-folder Makefiles
#

#
# Compiler config
#
PLATFORM  = gcctm_gcc_linux_x86_64_opt
CXX       = g++
CC       ?= gcc
CXXFLAGS += -O3 -ggdb -m64 -march=native -mtune=native -msse2 -mfpmath=sse -fgnu-tm
LDFLAGS  += -ldl -lrt -lpthread -m64

#
# Options to pass to STM files
#
CXXFLAGS += -DSTM_API_GCCTM
CXXFLAGS += -DSTM_CC_GCC
CXXFLAGS += -DSTM_OS_LINUX
CXXFLAGS += -DSTM_CPU_X86
CXXFLAGS += -DSTM_BITS_64
CXXFLAGS += -DSTM_OPT_O3
CXXFLAGS += -DSTM_WS_BYTELOG
CXXFLAGS += -DGCCTM