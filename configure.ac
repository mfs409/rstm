dnl
dnl Copyright (C) 2012
dnl  University of Rochester Department of Computer Science
dnl   and
dnl  Lehigh University Department of Computer Science and Engineering
dnl
dnl License: Modified BSD
dnl          Please see the file LICENSE.RSTM for licensing information
dnl

dnl I'm not aware of any 2.68-specific things that we do, but I'm also not sure
dnl that there aren't any. 2.68 is the current version of autoconf so we'll just
dnl be safe and require it.
AC_PREREQ(2.68)

dnl Initialize autoconf for RSTMv7. Email Luke with questions or bugs (should also
dnl file PRs on rstm.googlecode.com). TODO: can we use some sort of
dnl rstm.googlecode.com contact information, instead of Luke's email directly?
AC_INIT([RSTM], [8.0], [luked@cs.rochester.edu])

dnl This is a standard safety check. See
dnl https://www.google.com/search?q=ac_config_srcdir for details.
AC_CONFIG_SRCDIR(LICENSE.RSTM)

dnl Configure auxiliary directories. These shouldn't need be changed unless our
dnl directory structure changes
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])

dnl We use the target information to select the correct checkpoint asm files to
dnl build.
AC_CANONICAL_TARGET

dnl Initialize automake.
dnl
dnl - 1.11: The current automake version during development. I'm not aware of
dnl         using any 1.11-specific automake-isms, but I'm not an expert and
dnl         wouldn't really know if I did. It seems safest to require this
dnl         version (only developers that need to run autogen need this).
dnl
dnl - foreign: We don't use the GNU layout---this suppresses warnings.
dnl
dnl - silent-rules: We want our build to have the capability to use
dnl                 silent-rules. This sets this capability up. The
dnl                 AM_SILENT_RULES([yes]) macro selects silent rules as the
dnl                 default. This can be overridden by --disable-silent-rules,
dnl                 or 'make V=1'.
dnl
dnl - subdir-objects: If we add build targets, we'd like to allow subdirectory
dnl                   builds.
dnl - '-Wall -Werror': Refers to automake, not building object files.
dnl
AM_INIT_AUTOMAKE([1.11 foreign silent-rules subdir-objects -Wall -Werror])
AM_SILENT_RULES([yes])

dnl Do a standard search for ar and as, both of which we need.
AM_PROG_AR
AM_PROG_AS

dnl Our primary target is a library, so we use libtool to generate it. This macro
dnl sets up the necessary stuff to make this happen.
dnl
dnl - disable-shared: By default, we build libitm.a, and not libitm.so/dylib. This
dnl                   can be adjusted at configuration time using --enable-shared
dnl                   and/or --disable-static.
dnl
AC_DISABLE_SHARED
AC_PROG_LIBTOOL

dnl We build C++ sources exclusively at the moment, except for asm files which
dnl use CC via CCAS
AC_PROG_CXX

dnl Generates a Makefile from Makefile.in (which is generated from Makefile.am).
AC_CONFIG_FILES([Makefile
                 lib/Makefile])

dnl Allow the user to disable assertions using --disable-assert (they're enabled
dnl by default).
AC_HEADER_ASSERT

dnl dnl Allow the user to decide if we should build the adaptive library.
dnl AC_ARG_ENABLE([adaptm],
dnl     AS_HELP_STRING([--enable-adaptm], [Enable libAdapTM (defualt=NO)]))
        
dnl if test "x$enable_adaptm" != xno
dnl then
dnl fi

dnl dnl Allow the user to decide if we should build the adaptive library.
dnl AC_ARG_ENABLE([adaptm],
dnl     AS_HELP_STRING([--enable-adaptm], [Enable libAdapTM (defualt=NO)]))
        
dnl if test "x$enable_adaptm" != xno
dnl then
dnl fi

dnl dnl Allow the user to build libitm with internal SIMD instructions (for example,
dnl dnl SIMD BloomFilter operations---disabled by default).
dnl AC_ARG_ENABLE([simd],
dnl     AS_HELP_STRING([--enable-simd], [Use SIMD instructions internally (defualt=NO)]))
        
dnl if test "x$enable_simd" != xno
dnl then
dnl     AC_SUBST([RSTM_D_SIMD], [-DRSTM_SIMD])
dnl fi

dnl Allow users to build libitm with warnings treated as errors (disabled by
dnl default). 
AC_ARG_ENABLE([werror],
    AS_HELP_STRING([--enable-werror], [Enable -Werror (defualt=NO)]))

if test "x$enable_werror" != xno
then
   AC_SUBST([RSTM_W_ERROR], [-Werror])
fi

dnl Allow users to build libitm with link-time-optimization.
AC_ARG_ENABLE([lto],
    AS_HELP_STRING([--enable-lto], [Enable link-time optimization (defualt=NO)]))

if test "x$enable_lto" != xno
then
   AC_SUBST([RSTM_F_LTO], [-flto])
fi

dnl Make sure that we build the correct checkpoint file.
AM_CONDITIONAL([RSTM_INTEL], [ @<:@@<:@ "$target_cpu" =~ x86_64*|i?86 @:>@@:>@ ])
AM_CONDITIONAL([RSTM_SPARC], [ @<:@@<:@ "$target_cpu" =~ sparc* @:>@@:>@ ])

AC_OUTPUT