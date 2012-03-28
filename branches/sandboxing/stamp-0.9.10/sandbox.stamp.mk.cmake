# -*- Makefile -*-*
VPATH  += @CMAKE_CURRENT_SOURCE_DIR@/lib

ifndef DTMC
CXXFLAGS += -fno-exceptions
endif

CFLAGS   += -I@CMAKE_CURRENT_SOURCE_DIR@/lib
CXXFLAGS += -I@CMAKE_CURRENT_SOURCE_DIR@/lib