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
# library API, clang, Linux, x86_64, -O3
#
# Warning: This just handles platform configuration.  Everything else is
#          handled via per-folder Makefiles
#

#
# Compiler config
#
PLATFORM  = lib_clang_linux_x86_64_opt
CXX       = clang++
CC       ?= clang
CPPFLAGS += -I/u/luked/pub/libunwind/1.0.1/include
CXXFLAGS += -O3 -m64 -msse2 -fno-exceptions -fno-rtti -Wall -Wextra
LDFLAGS  += -ldl -lrt -lpthread -m64

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
