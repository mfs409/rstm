#
# Pull in the RSTM declarations
#

include ../lib/Targets.mk

#
# Update CFLAGS with RSTM locations
#

CFLAGS   += -Wall -I$(STMLIBDIR) -I$(STMINCLUDEDIR) -DSTM

#
# Path to the stm.h file
#

STMINCLUDEDIR := ../include

#
# Path to the stm libXYZ.a files
#

STMLIBDIR := ../$(ODIR)

#
# We build the program multiple times, for each STM alg
#

TARGETS     = $(foreach alg,$(ALGNAMES),$(ODIR)/$(PROG).$(alg))

#
# Assuming this was included in a proper makefile, PROG will be defined
#

default: $(ODIR) $(TARGETS)

#
# The output directory
#

$(ODIR):
	@mkdir $@

#
# Rules for building individual benchmarks according to the specified API.
# Note that the lock and stm apis are split out
#

$(ODIR)/%.$(PROG).lockapi.o: $(PROG)/%.c
	@echo [CC] $< "-->" $@ 
	@$(CC) $(CFLAGS) -o $@ -c $< -DSTM_INST_CGL
$(ODIR)/%.$(PROG).lockapi.o: lib/%.c
	@echo [CC] $< "-->" $@ 
	@$(CC) $(CFLAGS) -o $@ -c $< -DSTM_INST_CGL
$(ODIR)/%.$(PROG).genericapi.o: $(PROG)/%.c
	@echo [CC] $< "-->" $@
	@$(CC) $(CFLAGS) -o $@ -c $< -DSTM_INST_STM
$(ODIR)/%.$(PROG).genericapi.o: lib/%.c
	@echo [CC] $< "-->" $@
	@$(CC) $(CFLAGS) -o $@ -c $< -DSTM_INST_STM

#
# Actual rules for linking to make executables.  Unfortunately, this is a
# little bit brittle, because the rules incorporate knowledge of which APIs
# are required by whigh libraries
#

CGLFILES = $(patsubst %, $(ODIR)/%.$(PROG).lockapi.o, $(BENCHNAMES) $(LIBNAMES))
STMFILES = $(patsubst %, $(ODIR)/%.$(PROG).genericapi.o, $(BENCHNAMES) $(LIBNAMES))
DEPS     = $(patsubst %.o, %.d, $(CGLFILES) $(STMFILES))

$(ODIR)/$(PROG).cgl: $(CGLFILES) $(STMLIBDIR)/libcgl.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS)

$(ODIR)/$(PROG).norec: $(STMFILES) $(STMLIBDIR)/libnorec.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 

$(ODIR)/$(PROG).tml: $(STMFILES) $(STMLIBDIR)/libtml.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 

$(ODIR)/$(PROG).cohortseager: $(STMFILES) $(STMLIBDIR)/libcohortseager.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 

$(ODIR)/$(PROG).cohorts: $(STMFILES) $(STMLIBDIR)/libcohorts.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 

$(ODIR)/$(PROG).ctokenturbo: $(STMFILES) $(STMLIBDIR)/libctokenturbo.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 

$(ODIR)/$(PROG).ctoken: $(STMFILES) $(STMLIBDIR)/libctoken.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 

$(ODIR)/$(PROG).llt: $(STMFILES) $(STMLIBDIR)/libllt.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 

$(ODIR)/$(PROG).oreceager: $(STMFILES) $(STMLIBDIR)/liboreceager.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 

$(ODIR)/$(PROG).oreceagerredo: $(STMFILES) $(STMLIBDIR)/liboreceagerredo.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 

$(ODIR)/$(PROG).oreclazy: $(STMFILES) $(STMLIBDIR)/liboreclazy.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 

$(ODIR)/$(PROG).orecela: $(STMFILES) $(STMLIBDIR)/liborecela.a
	@echo [LD] $@ 
	@$(CXX) -o $@ $^ $(LDFLAGS) 

$(ODIR)/$(PROG).orecala: $(STMFILES) $(STMLIBDIR)/liborecala.a
	@echo [LD] $@
	@$(CXX) -o $@ $^ $(LDFLAGS)

-include $(DEPS)
