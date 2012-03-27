# Build our set of test applications. They're all written using the C++ TM
# standard, so they need to have -fgnu-tm during compilation. We don't want
# -fgnu-tm at link time though, because we want to link with our project's
# libitm.a, and---for whatever reason---automake passes CFLAGS/CXXFLAGS to the
# linker. Cludgy solution is to use CPPFLAGS, even though -fgnu-tm is a compiler
# flag.
AM_CPPFLAGS = -fgnu-tm

check_PROGRAMS = simple empty

empty_SOURCES  = ../src/empty.cpp
empty_LDADD    = $(top_builddir)/libitm/libcgl.la

simple_SOURCES = ../src/simple.cpp
simple_LDADD   = $(top_builddir)/libitm/libcgl.la
