TORULES  := ../../..
SOURCES  := ../lib/bitmap.c ../lib/hash.c ../lib/hashtable.c ../lib/pair.c \
            ../lib/random.c ../lib/list.c ../lib/mt19937ar.c ../lib/thread.c \
            ../lib/vector.c \
            gene.c genome.c segments.c sequencer.c table.c
CFLAGS   += -I../lib -I../.. -I../../include -I$(TORULES)/../build/rstm/include
CFLAGS   += -DLIST_NO_DUPLICATES -DCHUNK_STEP1=12
LLVM-GCC := llvm-g++
EXECS    := genome

-include $(TORULES)/Makefile.rules

test: genome
	@echo "sandboxing test run: genome"
	./genome -g16384 -s64 -n16777216