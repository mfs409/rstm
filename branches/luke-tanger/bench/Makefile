CXX               = g++-4.5.3
DRAGONEGG         = /u/luked/local/source/dragonegg-2.9/dragonegg.so
LLC               = llc
LLVM-AS           = llvm-as
LLVM-GXX          = llvm-g++
LLVM-DIS          = llvm-dis
LLVM-OPT          = opt
TANGERLLVM        = /u/luked/local/llvm-2.8
TANGERLLVM-CONFIG = $(TANGERLLVM)/bin/llvm-config
TANGEROPT         = $(TANGERLLVM)/bin/opt
TANGERLLVM-AS     = $(TANGERLLVM)/bin/llvm-as
TANGERBUILD       = $(shell $(TANGERLLVM-CONFIG) --build-mode)
TANGERSRC         = /u/luked/local/source/DTMC_R3/tanger_R3
TANGERPASS        = $(TANGERSRC)/$(TANGERBUILD)/lib/libtanger.so
SANDBOXPASS       = /u/luked/sandboxing/build/sandbox/$(TANGERBUILD)/lib/sandbox.so 

CXXFLAGS += -O3 -I. -I.. -I../include -I../../../build/rstm/include -DSTM_API_TANGER
CXXFLAGS += -fno-rtti -fno-exceptions

BENCHMARKS = CounterBench TreeBench DListBench DisjointBench ForestBench \
             HashBench ListBench MCASBench ReadNWrite1Bench ReadWriteNBench \
             TreeOverwriteBench WWPathologyBench

all: $(BENCHMARKS)

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

# Extended this section so that we can do multi-source tanger builds. The point
# was to avoid dead-code elimination from causing trouble in the Tree
# benchmarks.

CounterBench.merged.bc: CounterBench.bc
	llvm-link -o $@ $^

TreeBench.merged.bc: TreeBench.bc Tree.bc
	llvm-link -o $@ $^

DListBench.merged.bc: DListBench.bc
	llvm-link -o $@ $^

DisjointBench.merged.bc: DisjointBench.bc
	llvm-link -o $@ $^

ForestBench.merged.bc: ForestBench.bc Tree.bc
	llvm-link -o $@ $^

HashBench.merged.bc: HashBench.bc
	llvm-link -o $@ $^

ListBench.merged.bc: ListBench.bc
	llvm-link -o $@ $^

MCASBench.merged.bc: MCASBench.bc
	llvm-link -o $@ $^

ReadNWrite1Bench.merged.bc: ReadNWrite1Bench.bc
	llvm-link -o $@ $^

ReadWriteNBench.merged.bc: ReadWriteNBench.bc
	llvm-link -o $@ $^

TreeOverwriteBench.merged.bc: TreeOverwriteBench.bc Tree.bc
	llvm-link -o $@ $^

WWPathologyBench.merged.bc: WWPathologyBench.bc
	llvm-link -o $@ $^

%.tanger.bc: %.merged.bc
	$(TANGEROPT) -load $(TANGERPASS) -tanger -tanger-whole-program \
	-tanger-indirect-auto -tanger-resolve-nontxnal -O3 -o $@ $^

%.sandboxed.bc: %.tanger.bc
	$(LLVM-OPT) -load $(SANDBOXPASS) -sandbox -o $@ $^

%.tanger.linked.bc: %.sandboxed.bc \
                    $(TANGERSRC)/$(TANGERBUILD)/lib/libtanger-stm.bc \
                    $(TANGERSRC)/$(TANGERBUILD)/lib/stmsupport.bc
	llvm-link -o $@ $^

%.tanger.linked.o: %.tanger.linked.bc
	llvmc -O3 -native -o $@ -c $^ 

bmharness.o: bmharness.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $^

$(BENCHMARKS): % : %.tanger.linked.o bmharness.o
	$(CXX) $(LDFLAGS) -o $@ $^ -lrt -lpthread 

clean:
	rm -f *.bc *.ll *.o *.s
	rm -f $(BENCHMARKS)