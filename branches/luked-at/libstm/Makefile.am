ACLOCAL_AMFLAGS = -I m4
lib_LTLIBRARIES = libstm.la

libstm_la_SOURCES =   txthread.cpp \
  inst.cpp \
  types.cpp \
  profiling.cpp \
  WBMMPolicy.cpp \
  irrevocability.cpp \
  algs/algs.cpp \
  algs/biteager.cpp \
  algs/biteagerredo.cpp \
  algs/bitlazy.cpp \
  algs/byear.cpp \
  algs/byeau.cpp \
  algs/byteeager.cpp \
  algs/byteeagerredo.cpp \
  algs/bytelazy.cpp \
  algs/cgl.cpp \
  algs/ctoken.cpp \
  algs/ctokenturbo.cpp \
  algs/llt.cpp \
  algs/mcs.cpp \
  algs/nano.cpp \
  algs/norec.cpp \
  algs/norecprio.cpp \
  algs/oreau.cpp \
  algs/orecala.cpp \
  algs/oreceager.cpp \
  algs/oreceagerredo.cpp \
  algs/orecela.cpp \
  algs/orecfair.cpp \
  algs/oreclazy.cpp \
  algs/pipeline.cpp \
  algs/profiletm.cpp \
  algs/ringala.cpp \
  algs/ringsw.cpp \
  algs/serial.cpp \
  algs/profileapp.cpp \
  algs/swiss.cpp \
  algs/ticket.cpp \
  algs/tli.cpp \
  algs/tml.cpp \
  algs/tmllazy.cpp \
  policies/cbr.cpp \
  policies/policies.cpp \
  policies/static.cpp


libstm_la_CPPFLAGS = $(RSTM_D_GRANULARITY)
libstm_la_CPPFLAGS += $(RSTM_D_HISTOGRAM)
libstm_la_CPPFLAGS += $(RSTM_D_PROFILETM)
libstm_la_CPPFLAGS += $(RSTM_D_STACK_FILTER)
libstm_la_CPPFLAGS += $(RSTM_D_CANCEL_AND_THROW)
libstm_la_CPPFLAGS += $(RSTM_D_SIMD)

pkgconfigdir = $(libdir)/pkgconfig
nodist_pkgconfig_DATA = libstm.pc

libstm.pc: $(srcdir)/libstm.pc.in
	$(AM_V_GEN)$(SED) -e 's![@]prefix[@]!$(prefix)!g' \
					  -e 's![@]exec_prefix[@]!$(exec_prefix)!g' \
					  -e 's![@]includedir[@]!$(includedir)!g' \
					  -e 's![@]libdir[@]!$(libdir)!g' \
					  -e 's![@]PACKAGE_VERSION[@]!$(PACKAGE_VERSION)!g' \
					  -e 's![@]libstm_CPPFLAGS[@]!$(libstm_la_CPPFLAGS) $(CPPFLAGS)!g' \
	$^ > $@
