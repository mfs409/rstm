$(ODIR)/%.o: $(LIBDIR)/%.cpp $(CONFIGH)
	@echo [CXX] $< "-->" $@
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

$(ODIR)/%.cgl: $(BENCHDIR)/%.cpp $(CONFIGH) $(ODIR)/cgl.o
	@echo [CXX] $< "-->" $@ 
	@$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS) -DSTM_INST_CGL $(ODIR)/cgl.o

$(ODIR)/%.norec: $(BENCHDIR)/%.cpp $(CONFIGH) $(ODIR)/norec.o $(ODIR)/WBMMPolicy.o
	@echo [CXX] $< "-->" $@ 
	@$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS) -DSTM_INST_GENERIC $(ODIR)/norec.o $(ODIR)/WBMMPolicy.o

$(ODIR)/%.tml: $(BENCHDIR)/%.cpp $(CONFIGH) $(ODIR)/tml.o $(ODIR)/WBMMPolicy.o
	@echo [CXX] $< "-->" $@ 
	@$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS) -DSTM_INST_GENERIC $(ODIR)/tml.o $(ODIR)/WBMMPolicy.o

$(ODIR)/%.cohortseager: $(BENCHDIR)/%.cpp $(CONFIGH) $(ODIR)/cohortseager.o $(ODIR)/WBMMPolicy.o
	@echo [CXX] $< "-->" $@ 
	@$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS) -DSTM_INST_GENERIC $(ODIR)/cohortseager.o $(ODIR)/WBMMPolicy.o

$(ODIR)/%.cohorts: $(BENCHDIR)/%.cpp $(CONFIGH) $(ODIR)/cohorts.o $(ODIR)/WBMMPolicy.o
	@echo [CXX] $< "-->" $@ 
	@$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS) -DSTM_INST_GENERIC $(ODIR)/cohorts.o $(ODIR)/WBMMPolicy.o


#
# [mfs] ignore this; it's a playground for figuring out how to get everything
#       to build without too many build lines.  This will go away once I put
#       it in use
#

TEST2 = $(ODIR)/CounterBench.cgl
testit: $(TEST2)
	@echo $(patsubst $(ODIR)/, , $(patsubst %., , $(TEST)))
	@echo $(patsubst .%, , $(TEST))
	@echo $(notdir $(TEST2))
	@echo $(suffix $(TEST2))
	@echo $(basename $(notdir $(TEST2)))
	@echo $(subst ., , $(suffix $(TEST2)))
	@echo $(ODIR)/$(basename $(notdir $(@))).instnone
	@echo $(ODIR)/$(subst .,,$(suffix $(@))).o

