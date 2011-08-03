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

CXXFLAGS += -O3 -I. -I.. -I../include -I../../../build/rstm/include -DSTM_API_TANGER
CXXFLAGS += -fno-exceptions #-fno-rtti

BENCHMARKS = CounterBench TreeBench DListBench DisjointBench ForestBench \
             HashBench ListBench MCASBench ReadNWrite1Bench ReadWriteNBench \
             TreeOverwriteBench WWPathologyBench

SANDBOXPASS_stats   = -sandbox-stats

SANDBOXPASS_trivial = -sandbox-trivial
SANDBOXLIBS_trivial = $(SANDBOXBUILD)/lib/sandboxing_stats.bc

.PRECIOUS: %.tanger.bc
.PHONY: all clean

all: $(BENCHMARKS)

%.bc: %.cpp
	$(LLVM-GXX) $(CXXFLAGS) -emit-llvm -c $^ -o $@

%.ll: %.bc
	$(LLVM-DIS) $^

# Extended this section so that we can do multi-source tanger builds. The point
# was to avoid dead-code elimination from causing trouble in the Tree
# benchmarks. The multi-source build doesn't actually help at all here, llvm
# still eliminates the lookup routine during LTO. I've added some useless work
# to try and sort that out.

CounterBench.merged.bc: CounterBench.bc
	$(LLVM-LINK) -o $@ $^

TreeBench.merged.bc: TreeBench.bc Tree.bc
	$(LLVM-LINK) -o $@ $^

DListBench.merged.bc: DListBench.bc
	$(LLVM-LINK) -o $@ $^

DisjointBench.merged.bc: DisjointBench.bc
	$(LLVM-LINK) -o $@ $^

ForestBench.merged.bc: ForestBench.bc Tree.bc
	$(LLVM-LINK) -o $@ $^

HashBench.merged.bc: HashBench.bc
	$(LLVM-LINK) -o $@ $^

ListBench.merged.bc: ListBench.bc
	$(LLVM-LINK) -o $@ $^

MCASBench.merged.bc: MCASBench.bc
	$(LLVM-LINK) -o $@ $^

ReadNWrite1Bench.merged.bc: ReadNWrite1Bench.bc
	$(LLVM-LINK) -o $@ $^

ReadWriteNBench.merged.bc: ReadWriteNBench.bc
	$(LLVM-LINK) -o $@ $^

TreeOverwriteBench.merged.bc: TreeOverwriteBench.bc Tree.bc
	$(LLVM-LINK) -o $@ $^

WWPathologyBench.merged.bc: WWPathologyBench.bc
	$(LLVM-LINK) -o $@ $^

%.tanger.bc: %.merged.bc
	$(TANGEROPT) -load $(TANGERSO) -tanger -tanger-whole-program \
	-tanger-indirect-auto -tanger-resolve-nontxnal -O3 -o $@ $^

%.sandboxed.bc: %.tanger.bc
	$(LLVM-OPT) -load $(SANDBOXSO) $(SANDBOXPASS_$(SANDBOX)) -stats -debug -o $@ $^ &> $*.dat

%.tanger.linked.bc: %.sandboxed.bc \
                    $(TANGERSRC)/$(TANGERBUILD)/lib/libtanger-stm.bc \
                    $(TANGERSRC)/$(TANGERBUILD)/lib/stmsupport.bc \
					$(SANDBOXLIBS_$(SANDBOX))
	$(LLVM-LINK) -o $@ $^

%.tanger.linked.o: %.tanger.linked.bc
	$(LLVMC) -O3 -native -o $@ -c $^ 

bmharness.o: bmharness.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $^

$(BENCHMARKS): % : %.tanger.linked.o bmharness.o
	$(CXX) $(LDFLAGS) -o $@ $^ -lpthread 

clean:
	rm -f *.bc *.ll *.o *.s *.dot *.dat
	rm -f $(BENCHMARKS)