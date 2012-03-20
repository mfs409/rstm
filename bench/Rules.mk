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
# microbenchmarks
#

#
# It's useful to keep all intermediate .o and .h files around, so we list
# them all here (we could use precious instead)
#

benchmarks: $(ODIR) $(CONFIGH) $(BENCHOFILES) $(BENCHEXES)

#
# Rules for building individual benchmarks according to the specified API
#
$(ODIR)/%.lockapi.o: $(BENCHDIR)/%.cpp
	@echo [CXX] $< "-->" $@ 
	@$(CXX) $(CXXFLAGS) -o $@ -c $^ $(LDFLAGS) -DSTM_INST_CGL

$(ODIR)/%.genericapi.o: $(BENCHDIR)/%.cpp
	@echo [CXX] $< "-->" $@ 
	@$(CXX) $(CXXFLAGS) -o $@ -c $^ $(LDFLAGS) -DSTM_INST_STM

#
# Actual rules for linking to make executables.  Unfortunately, this is a
# little bit brittle, because the rules incorporate knowledge of which APIs
# are required by whigh libraries
#

$(ODIR)/%.cgl: $(ODIR)/%.lockapi.o $(ODIR)/libcgl.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)

$(ODIR)/%.norec: $(ODIR)/%.genericapi.o $(ODIR)/libnorec.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)

$(ODIR)/%.tml: $(ODIR)/%.genericapi.o $(ODIR)/libtml.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)

$(ODIR)/%.cohortseager: $(ODIR)/%.genericapi.o $(ODIR)/libcohortseager.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)

$(ODIR)/%.cohorts: $(ODIR)/%.genericapi.o $(ODIR)/libcohorts.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)

$(ODIR)/%.ctokenturbo: $(ODIR)/%.genericapi.o $(ODIR)/libctokenturbo.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)

$(ODIR)/%.ctoken: $(ODIR)/%.genericapi.o $(ODIR)/libctoken.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)

$(ODIR)/%.llt: $(ODIR)/%.genericapi.o $(ODIR)/libllt.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)

$(ODIR)/%.oreceager: $(ODIR)/%.genericapi.o $(ODIR)/liboreceager.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)

$(ODIR)/%.oreceagerredo: $(ODIR)/%.genericapi.o $(ODIR)/liboreceagerredo.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)

$(ODIR)/%.oreclazy: $(ODIR)/%.genericapi.o $(ODIR)/liboreclazy.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)

$(ODIR)/%.orecela: $(ODIR)/%.genericapi.o $(ODIR)/liborecela.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)

$(ODIR)/%.orecala: $(ODIR)/%.genericapi.o $(ODIR)/liborecala.a
	@echo [LD] $@
	@$(CXX) -o $@ $^ $(LDFLAGS)
