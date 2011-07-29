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
TANGERPASS        = $(TANGERSRC)/$(TANGERBUILD)/lib/libtanger.dylib
SANDBOXPASS       = /Users/luked/Work/sandboxing/build/transform/$(TANGERBUILD)/lib/sandbox.dylib

CXXFLAGS += -O3 -I. -I.. -I../include -I../../../build/rstm/include -DTANGER
CXXFLAGS += -m32 -fno-exceptions
LDFLAGS  += -m32

SOURCES = dwyer.cpp edge.cpp edge_set.cpp mesh.cpp my_thread.cpp point.cpp \
          queues.cpp side.cpp worker.cpp

all: mesh

%.bc: %.cpp
	$(LLVM-GXX) $(CXXFLAGS) -emit-llvm -c $^ -o $@

mesh.merged.bc: $(SOURCES:.cpp=.bc)
	$(LLVM-LINK) -o $@ $^

%.tanger.bc: %.merged.bc
	$(TANGEROPT) -load $(TANGERPASS) -tanger -tanger-whole-program \
    -tanger-indirect-auto -tanger-resolve-nontxnal -O3 -o $@ $^

%.sandboxed.bc: %.tanger.bc
	$(LLVM-OPT) -load $(SANDBOXPASS) -sandbox -stats -debug-only=sandbox \
       -o $@ $^ &> $@.info

%.linked.bc: %.sandboxed.bc $(TANGERSRC)/$(TANGERBUILD)/lib/libtanger-stm.bc \
                            $(TANGERSRC)/$(TANGERBUILD)/lib/stmsupport.bc
	$(LLVM-LINK) -o $@ $^

%.linked.o: %.linked.bc
	$(LLVMC) -m32 -O3 -native -o $@ -c $^ 

mesh: mesh.linked.o
	$(CXX) $(LDFLAGS) -o $@ $^ -lpthread 

clean:
	rm -f *.bc *.ll *.o *.s
	rm -f mesh