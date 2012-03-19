#
#  Copyright (C) 2011
#  University of Rochester Department of Computer Science
#    and
#  Lehigh University Department of Computer Science and Engineering
# 
# License: Modified BSD
#          Please see the file LICENSE.RSTM for licensing information
#

all: $(ODIR) $(CONFIGH) $(LIBS) $(SUPTS) $(ORECS) $(PREBENCH) $(BENCHES) $(LAZYS)
	@echo "Build complete"

$(ODIR):
	@mkdir $@

clean:
	@rm -rf $(ODIR)
	@echo $(ODIR) clean

#
# Rule for building individual files in the lib folder
#
$(ODIR)/%.o: $(LIBDIR)/%.cpp $(CONFIGH)
	@echo [CXX] $< "-->" $@
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

#
# Rule for building individual benchmarks according to the specified API
#
$(ODIR)/%.lockapi.o: $(BENCHDIR)/%.cpp
	@echo [CXX] $< "-->" $@ 
	@$(CXX) $(CXXFLAGS) -o $@ -c $^ $(LDFLAGS) -DSTM_INST_CGL

$(ODIR)/%.genericapi.o: $(BENCHDIR)/%.cpp
	@echo [CXX] $< "-->" $@ 
	@$(CXX) $(CXXFLAGS) -o $@ -c $^ $(LDFLAGS) -DSTM_INST_GENERIC

#
# All executables depend on config.h.  Putting it here lets us use cleaner
# autorules later
#
$(BENCHES): $(CONFIGH)

#
# Actual rules for linking to make executables
#

$(ODIR)/%.cgl: $(ODIR)/%.lockapi.o $(ODIR)/cgl.o $(SUPTS)
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)

$(ODIR)/%.norec: $(ODIR)/%.genericapi.o $(ODIR)/norec.o $(SUPTS) $(LAZYS)
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) -DSTM_INST_GENERIC

$(ODIR)/%.tml: $(ODIR)/%.genericapi.o $(ODIR)/tml.o $(SUPTS)
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) -DSTM_INST_GENERIC

$(ODIR)/%.cohortseager: $(ODIR)/%.genericapi.o $(ODIR)/cohortseager.o $(SUPTS) $(LAZYS) $(ORECS)
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) -DSTM_INST_GENERIC

$(ODIR)/%.cohorts: $(ODIR)/%.genericapi.o $(ODIR)/cohorts.o $(SUPTS) $(LAZYS) $(ORECS)
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) -DSTM_INST_GENERIC

$(ODIR)/%.ctokenturbo: $(ODIR)/%.genericapi.o $(ODIR)/ctokenturbo.o $(SUPTS) $(LAZYS) $(ORECS)
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) -DSTM_INST_GENERIC

$(ODIR)/%.ctoken: $(ODIR)/%.genericapi.o $(ODIR)/ctoken.o $(SUPTS) $(LAZYS) $(ORECS)
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) -DSTM_INST_GENERIC

$(ODIR)/%.llt: $(ODIR)/%.genericapi.o $(ODIR)/llt.o $(SUPTS) $(LAZYS) $(ORECS)
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) -DSTM_INST_GENERIC

$(ODIR)/%.oreceager: $(ODIR)/%.genericapi.o $(ODIR)/oreceager.o $(SUPTS) $(ORECS)
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) -DSTM_INST_GENERIC

$(ODIR)/%.oreceagerredo: $(ODIR)/%.genericapi.o $(ODIR)/oreceagerredo.o $(SUPTS) $(LAZYS) $(ORECS)
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) -DSTM_INST_GENERIC

$(ODIR)/%.oreclazy: $(ODIR)/%.genericapi.o $(ODIR)/oreclazy.o $(SUPTS) $(LAZYS) $(ORECS)
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) -DSTM_INST_GENERIC

$(ODIR)/%.orecela: $(ODIR)/%.genericapi.o $(ODIR)/orecela.o $(SUPTS) $(LAZYS) $(ORECS)
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) -DSTM_INST_GENERIC

$(ODIR)/%.orecala: $(ODIR)/%.genericapi.o $(ODIR)/orecala.o $(SUPTS) $(LAZYS) $(ORECS)
	@echo [LD] $@
	@$(CXX) -o $@ $^ $(LDFLAGS) -DSTM_INST_GENERIC
