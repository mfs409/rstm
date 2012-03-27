# Across-the-board CPP flags.
AM_CPPFLAGS  = $(RSTM_D_HISTOGRAM) $(RSTM_D_SIMD)
AM_CPPFLAGS += -I$(top_srcdir)

# Across-the-board CXXFLAGS
AM_CXXFLAGS  = -std=c++03
AM_CXXFLAGS += -Wall $(RSTM_W_ERROR) -Wno-variadic-macros
AM_CXXFLAGS += -fno-rtti -fno-exceptions $(RSTM_F_LTO)

# The libraries that we want to build.
lib_LTLIBRARIES = libcommon.la libcgl.la

# The common library is a "utility" library consisting of .o files that are
# common across the library implementations and only need to built once,
# per-configuration. 
libcommon_la_SOURCES = locks.cpp asm.cpp locks.h asm.h checkpoint.h

if RSTM_SPARC
libcommon_la_SOURCES += checkpoint-sparc.S
endif

if RSTM_INTEL
libcommon_la_SOURCES += checkpoint-intel.S
endif

libcommon_la_CXXFLAGS = -pedantic $(AM_CXXFLAGS)

# For each library we want to add the libary-specific sources, and the
# dependency on libcommon.
libcgl_la_SOURCES = stm-cgl.cpp
libcgl_la_LIBADD  = libcommon.la

