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
# library API, GCC, Linux, ia32, -O3
#
# Warning: This just handles platform configuration.  Everything else is
#          handled via per-folder Makefiles
#

#
# Compiler config
#
PLATFORM  = dragoneggtm_linux_ia32_opt
CXX       = g++ -fplugin=dragonegg
CC       ?= gcc -fplugin=dragonegg
CXXFLAGS += -O3 -ggdb -m32 -march=native -mtune=native -msse2 -mfpmath=sse -fgnu-tm
LDFLAGS  += -ldl -lrt -lpthread -m32 -fgnu-tm
ASFLAGS  += -m32

#
# Options to pass to STM files
#
CXXFLAGS += -DSTM_API_GCCTM
CXXFLAGS += -DSTM_CC_GCC
CXXFLAGS += -DSTM_OS_LINUX
CXXFLAGS += -DSTM_CPU_X86
CXXFLAGS += -DSTM_BITS_32
CXXFLAGS += -DSTM_OPT_O3
CXXFLAGS += -DSTM_WS_BYTELOG
