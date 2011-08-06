TORULES = ..

CXXFLAGS += -DSTM_API_TANGER

EXECS := CounterBench TreeBench DListBench DisjointBench ForestBench \
         HashBench ListBench MCASBench ReadNWrite1Bench ReadWriteNBench \
         TreeOverwriteBench WWPathologyBench

-include $(TORULES)/Makefile.rules

# Add the harness to the link phase for all of the benchmarks
$(EXECS): bmharness.o

# We're not using the "SOURCES" variable to build the benchmarks, because we
# have a large number of targets and we don't want to add SOURCES_target into
# the Makefile.rules. We just override the merged phase here.

CounterBench.merged.bc: CounterBench.bc

TreeBench.merged.bc: TreeBench.bc Tree.bc

DListBench.merged.bc: DListBench.bc

DisjointBench.merged.bc: DisjointBench.bc

ForestBench.merged.bc: ForestBench.bc Tree.bc

HashBench.merged.bc: HashBench.bc

ListBench.merged.bc: ListBench.bc

MCASBench.merged.bc: MCASBench.bc

ReadNWrite1Bench.merged.bc: ReadNWrite1Bench.bc

ReadWriteNBench.merged.bc: ReadWriteNBench.bc

TreeOverwriteBench.merged.bc: TreeOverwriteBench.bc Tree.bc

WWPathologyBench.merged.bc: WWPathologyBench.bc

test: $(EXECS)
	for b in $^; \
	do \
		echo "sandboxing test run: $$b";\
		./$$b;\
	done
