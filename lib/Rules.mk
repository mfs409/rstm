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
# This makefile specifies the dependencies and rules for building RSTM
# libraries
#

#
# It's useful to keep all intermediate .o and .h files around, so we list
# them all here (we could use precious instead)
#

librstm: $(ODIR) $(CONFIGH) $(LIBS) $(LIBOFILES)

#
# Rule for building individual files in the lib folder.  Everything depends on config.h.
#
$(ODIR)/%.o: $(LIBDIR)/%.cpp
	@echo [CXX] $< "-->" $@
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

#
# Rules for making libXYZ.a files.  Not all libraries require all of the .o
# files that we build
#

$(ODIR)/libcgl.a: $(ODIR)/cgl.o $(COMMONOFILES)
	@echo [AR] $@ 
	@$(AR) cru $@ $^

$(ODIR)/libnorec.a: $(ODIR)/norec.o $(COMMONOFILES) $(LAZYOFILES)
	@echo [AR] $@ 
	@$(AR) cru $@ $^

$(ODIR)/libtml.a: $(ODIR)/tml.o $(COMMONOFILES)
	@echo [AR] $@ 
	@$(AR) cru $@ $^

$(ODIR)/libcohortseager.a: $(ODIR)/cohortseager.o $(COMMONOFILES) $(LAZYOFILES) $(ORECOFILES)
	@echo [AR] $@ 
	@$(AR) cru $@ $^

$(ODIR)/libcohorts.a: $(ODIR)/cohorts.o $(COMMONOFILES) $(LAZYOFILES) $(ORECOFILES)
	@echo [AR] $@ 
	@$(AR) cru $@ $^

$(ODIR)/libctokenturbo.a: $(ODIR)/ctokenturbo.o $(COMMONOFILES) $(LAZYOFILES) $(ORECOFILES)
	@echo [AR] $@ 
	@$(AR) cru $@ $^

$(ODIR)/libctoken.a: $(ODIR)/ctoken.o $(COMMONOFILES) $(LAZYOFILES) $(ORECOFILES)
	@echo [AR] $@ 
	@$(AR) cru $@ $^

$(ODIR)/libllt.a: $(ODIR)/llt.o $(COMMONOFILES) $(LAZYOFILES) $(ORECOFILES)
	@echo [AR] $@ 
	@$(AR) cru $@ $^

$(ODIR)/liboreceager.a: $(ODIR)/oreceager.o $(COMMONOFILES) $(ORECOFILES)
	@echo [AR] $@ 
	@$(AR) cru $@ $^

$(ODIR)/liboreceagerredo.a: $(ODIR)/oreceagerredo.o $(COMMONOFILES) $(LAZYOFILES) $(ORECOFILES)
	@echo [AR] $@ 
	@$(AR) cru $@ $^

$(ODIR)/liboreclazy.a: $(ODIR)/oreclazy.o $(COMMONOFILES) $(LAZYOFILES) $(ORECOFILES)
	@echo [AR] $@ 
	@$(AR) cru $@ $^

$(ODIR)/liborecela.a: $(ODIR)/orecela.o $(COMMONOFILES) $(LAZYOFILES) $(ORECOFILES)
	@echo [AR] $@ 
	@$(AR) cru $@ $^

$(ODIR)/liborecala.a: $(ODIR)/orecala.o $(COMMONOFILES) $(LAZYOFILES) $(ORECOFILES)
	@echo [AR] $@
	@$(AR) cru $@ $^

-include $(LIBDEPS)
