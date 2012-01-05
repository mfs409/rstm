AC_PREREQ(2.68)
AC_INIT([libstm], [7.0], [luked@cs.rochester.edu])
AC_CONFIG_SRCDIR(txthread.cpp)
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_LIBOBJ_DIR([portable])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([1.11 foreign silent-rules subdir-objects -Wall -Werror])
AM_SILENT_RULES([yes])
AM_PROG_AR
LT_INIT([disable-shared])
AC_PROG_CXX
AC_PROG_SED
AC_CONFIG_FILES([Makefile])

AC_HEADER_ASSERT

AC_ARG_ENABLE([byte_logging],
    AS_HELP_STRING([--enable-byte-logging],
        [Enable byte granularity conflict tracking (default=NO)]))

AS_IF([test "x$enable_byte_logging" != "xno"],
    [AC_SUBST([RSTM_D_GRANULARITY], [-DSTM_WS_BYTELOG])],
    [AC_SUBST([RSTM_D_GRANULARITY], [-DSTM_WS_WORDLOG])])

AC_ARG_ENABLE([histogram],
    AS_HELP_STRING([--enable-histogram],
        [Enable histogram of aborts (defualt=NO)]))
        
AS_IF([test "x$enable_histogram" != "xno"],
    [AC_SUBST([RSTM_D_HISTOGRAM], [-DSTM_COUNTCONSEC_YES])])

AC_ARG_ENABLE([profiletm],
    AS_HELP_STRING([--enable-profiletm],
        [Enable profiling [all|yes|pathology|no] (defualt=NO)]))
        
AS_IF([test "x$enable_profiletm" != "xno"],
    [AC_SUBST([RSTM_D_PROFILETM], [-DSTM_PROFILETMTRIGGER_ALL])])

AC_ARG_ENABLE([stack_filter],
    AS_HELP_STRING([--enable-stack-filter],
        [Filter writes to suppress stack logging (defualt=NO)]))
        
AS_IF([test "x$enable_stack_filter" != "xno"],
    [AC_SUBST([RSTM_D_STACK_FILTER], [-DSTM_PROTECT_STACK])])
    
AC_ARG_ENABLE([abort_on_throw],
    AS_HELP_STRING([--enable-cancel-and-throw],
        [Support C++ cancel-and-throw semantics (defualt=NO)]))
        
AS_IF([test "x$enable_abort_on_throw" != "xno"],
    [AC_SUBST([RSTM_D_CANCEL_AND_THROW], [-DSTM_ABORT_ON_THROW])])

AC_ARG_ENABLE([simd],
    AS_HELP_STRING([--enable-simd],
        [Use SIMD instructions internally (defualt=NO)]))
        
AS_IF([test "x$enable_simd" != "xno"],
    [AC_SUBST([RSTM_D_SIMD], [-DSTM_USE_SSE])])

AC_OUTPUT