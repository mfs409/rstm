CXX               = g++-4.5.3
DRAGONEGG         = /u/luked/local/source/dragonegg-2.9/dragonegg.so
LLC               = llc
LLVM-AS           = llvm-as
LLVM-GXX          = llvm-g++
LLVM-DIS          = llvm-dis
LLVM-OPT          = opt
LLVM-CONFIG       = llvm-config
TANGERLLVM        = /u/luked/local/llvm-2.8
TANGERLLVM-CONFIG = $(TANGERLLVM)/bin/llvm-config
TANGEROPT         = $(TANGERLLVM)/bin/opt
TANGERLLVM-AS     = $(TANGERLLVM)/bin/llvm-as
TANGERBUILD       = $(shell $(TANGERLLVM-CONFIG) --build-mode)
TANGERSRC         = /u/luked/local/source/DTMC_R3/tanger_R3
TANGERPASS        = $(TANGERSRC)/$(TANGERBUILD)/lib/libtanger.so
SANDBOXBUILD      = $(shell $(LLVM-CONFIG) --build-mode)
SANDBOXPASS       = /u/luked/sandboxing/build/sandbox/$(SANDBOXBUILD)/lib/sandbox.so 

CXXFLAGS += -O3 -I. -I.. -I../include -I../../../build/rstm/include -DTANGER
CXXFLAGS += -m32 -fno-exceptions
LDFLAGS  += -m32

SOURCES = dwyer.cpp edge.cpp edge_set.cpp mesh.cpp my_thread.cpp point.cpp \
          queues.cpp side.cpp worker.cpp

all: mesh

ifndef USE_DRAGONEGG
%.bc: %.cpp
	$(LLVM-GXX) $(CXXFLAGS) -emit-llvm -c $^ -o $@
else
%.ll: %.cpp
	$(CXX) -fplugin=$(DRAGONEGG) $(CXXFLAGS) -flto -S $^ -o $@

%.bc: %.ll
	$(LLVM-AS) -o $@ $^
endif

%.ll: %.bc
	$(LLVM-DIS) $^

mesh.merged.bc: $(SOURCES:.cpp=.bc)
	llvm-link -o $@ $^

%.tanger.bc: %.merged.bc
	$(TANGEROPT) -load $(TANGERPASS) -tanger -tanger-whole-program \
    -tanger-indirect-auto -tanger-resolve-nontxnal -O3 -o $@ $^

%.sandboxed.bc: %.tanger.bc
	$(LLVM-OPT) -load $(SANDBOXPASS) -sandbox -o $@ $^

%.linked.bc: %.sandboxed.bc $(TANGERSRC)/$(TANGERBUILD)/lib/libtanger-stm.bc \
                            $(TANGERSRC)/$(TANGERBUILD)/lib/stmsupport.bc
	llvm-link -o $@ $^

%.linked.o: %.linked.bc
	llvmc -m32 -O3 -native -o $@ -c $^ 

bmharness.o: bmharness.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $^

mesh: mesh.linked.o
	$(CXX) $(LDFLAGS) -o $@ $^ -lrt -lpthread 

clean:
	rm -f *.bc *.ll *.o *.s
	rm -f mesh