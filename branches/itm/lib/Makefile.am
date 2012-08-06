# Across-the-board CPP flags.
AM_CPPFLAGS  = $(RSTM_D_SIMD)
AM_CPPFLAGS += -I$(top_srcdir)

# Across-the-board CXXFLAGS
AM_CXXFLAGS  = -std=c++03
AM_CXXFLAGS += -Wall -Wextra -pedantic -Wno-variadic-macros $(RSTM_W_ERROR)
AM_CXXFLAGS += -fno-rtti -fno-exceptions $(RSTM_F_LTO)

# The libraries that we want to build.
lib_LTLIBRARIES = libCGL.la libcommon.la libTML.la libNOrec.la

# The common library is a "utility" library consisting of .o files that are
# common across the library implementations and only need to built once,
# per-configuration. 
libcommon_la_SOURCES = WBMMPolicy.cpp Common.cpp CommonOrec.cpp tx.cpp \
                       adaptivity.cpp gcc.cpp 

if RSTM_SPARC
libcommon_la_SOURCES += $(top_srcdir)/alt-license/rand_r.cpp
endif

if RSTM_INTEL
endif

libcommon_la_CXXFLAGS = -pedantic $(AM_CXXFLAGS)

# For each library we want to add the libary-specific sources, and the
# dependency on libcommon.
libCGL_la_SOURCES = CGL.cpp
libCGL_la_LIBADD  = libcommon.la

libTML_la_SOURCES = TMP.cpp checkpoint.S
libTML_la_LIBADD  = libcommon.la

libNOrec_la_SOURCES = NOrec.cpp checkpoint.S
libNOrec_la_LIBADD  = libcommon.la