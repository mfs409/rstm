SANDBOX          ?= trivial

CXX               = g++
LLVM              = /Users/luked/Work/sandboxing/local/llvm-2.9
TANGERLLVM        = /Users/luked/Work/sandboxing/local/llvm-2.8

LLC               = $(LLVM)/bin/llc
LLVM-AS           = $(LLVM)/bin/llvm-as
LLVM-GXX          = llvm-g++
LLVM-DIS          = $(LLVM)/bin/llvm-dis
LLVM-OPT          = $(LLVM)/bin/opt
LLVM-LINK         = $(LLVM)/bin/llvm-link
LLVMC             = $(LLVM)/bin/llvmc

TANGERLLVM-CONFIG = $(TANGERLLVM)/bin/llvm-config
TANGEROPT         = $(TANGERLLVM)/bin/opt
TANGERLLVM-AS     = $(TANGERLLVM)/bin/llvm-as
TANGERBUILD       = $(shell $(TANGERLLVM-CONFIG) --build-mode)
TANGERSRC         = /Users/luked/Work/sandboxing/externals/tanger_R3
TANGERSO          = $(TANGERSRC)/$(TANGERBUILD)/lib/libtanger.dylib

SANDBOXBUILD      = /Users/luked/Work/sandboxing/build/transform/$(TANGERBUILD)
SANDBOXSO         = $(SANDBOXBUILD)/lib/sandbox.dylib

CXXFLAGS += -O3 -I. -I.. -I../include -I../../../build/rstm/include -DTANGER
CXXFLAGS += -fno-exceptions
#CXXFLAGS += -m32 -fno-exceptions
#LDFLAGS  += -m32

SANDBOXPASS_trivial = -sandbox-trivial
SANDBOXLIBS_trivial = $(SANDBOXBUILD)/lib/sandboxing_stats.bc

SANDBOXPASS_stats = -sandbox-stats

SOURCES = dwyer.cpp edge.cpp edge_set.cpp mesh.cpp my_thread.cpp point.cpp \
          queues.cpp side.cpp worker.cpp

.PRECIOUS: %.lto.bc %.tanger.bc %.sandboxed.bc
.PHONY: all clean

all: mesh

%.bc: %.cpp
	$(LLVM-GXX) $(CXXFLAGS) -emit-llvm -c $^ -o $@

%.merged.bc: $(SOURCES:.cpp=.bc)
	$(LLVM-LINK) -o $@ $^

%.lto.bc: %.merged.bc
	$(LLVM-OPT) -O3 -o $@ $^

%.tanger.bc: %.lto.bc
	$(TANGEROPT) -load $(TANGERSO) -tanger -tanger-whole-program \
    -tanger-indirect-auto -tanger-resolve-nontxnal -o $@ $^

%.sandboxed.bc: %.tanger.bc
	$(LLVM-OPT) -load $(SANDBOXSO) $(SANDBOXPASS_$(SANDBOX)) -stats -debug-only=sandbox \
       -o $@ $^ &> $*.log
# $(TANGERSRC)/$(TANGERBUILD)/lib/libtanger-stm.bc 
%.linked.bc: %.sandboxed.bc \
                            $(TANGERSRC)/$(TANGERBUILD)/lib/stmsupport.bc    \
                            $(SANDBOXLIBS_$(SANDBOX))
	$(LLVM-LINK) -o $@ $^

%.linked.o: %.linked.bc
	$(LLVMC) -native -o $@ -c $^ 

mesh: mesh.linked.o
	$(CXX) $(LDFLAGS) -o $@ $^ -lpthread 

clean:
	rm -f *.bc *.ll *.o *.s *.log
	rm -f mesh