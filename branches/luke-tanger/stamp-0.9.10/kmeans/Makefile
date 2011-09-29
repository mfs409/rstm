TORULES  := ../../..
SOURCES  := ../lib/mt19937ar.c ../lib/random.c ../lib/thread.c \
            cluster.c common.c kmeans.c normal.c
CFLAGS   += -I../lib -I../.. -I../../include -I$(TORULES)/../build/rstm/include
LLVM-GCC := llvm-g++
EXECS    := kmeans

-include $(TORULES)/Makefile.rules

test: kmeans
	@echo "sandboxing test run: kmeans-low"
	./kmeans -m15 -n15 -t0.00001 -i inputs/random-n65536-d32-c16.txt
	@echo "sandboxing test run: kmeans-high"
	./kmeans -m40 -n40 -t0.00001 -i inputs/random-n65536-d32-c16.txt