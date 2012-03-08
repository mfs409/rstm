AC_PREREQ(2.68)
AC_INIT([libstm], [7.0], [luked@cs.rochester.edu])
AC_CONFIG_SRCDIR(libitm.h)
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([1.11 foreign silent-rules subdir-objects -Wall -Werror])
AM_SILENT_RULES([yes])
AM_PROG_AR
LT_INIT([disable-shared])
AC_PROG_CXX
AC_PROG_SED
AC_CONFIG_FILES([Makefile])

AC_HEADER_ASSERT

AC_ARG_ENABLE([histogram],
    AS_HELP_STRING([--enable-histogram],
        [Enable histogram of aborts (defualt=NO)]))
        
AS_IF([test "x$enable_histogram" != "xno"],
    [AC_SUBST([RSTM_D_HISTOGRAM], [-DRSTM_HISTOGRAM])])

AC_ARG_ENABLE([simd],
    AS_HELP_STRING([--enable-simd],
        [Use SIMD instructions internally (defualt=NO)]))
        
AS_IF([test "x$enable_simd" != "xno"],
    [AC_SUBST([RSTM_D_SIMD], [-DSTM_SIMD])])

AC_OUTPUT