TORULES  := ../../..

SOURCES  := ../lib/bitmap.c \
            ../lib/list.c \
            ../lib/mt19937ar.c \
            ../lib/queue.c \
            ../lib/random.c \
            ../lib/thread.c \
            ../lib/vector.c \
            adtree.c \
            bayes.c \
            data.c \
            learner.c \
            net.c \
            sort.c

CFLAGS   += -I../lib -I../.. -I../../include -I$(TORULES)/../build/rstm/include
CFLAGS   += -DLIST_NO_DUPLICATES -DLEARNER_TRY_REMOVE -DLEARNER_TRY_REVERSE
LLVM-GCC := llvm-g++
EXECS    := bayes

-include $(TORULES)/Makefile.rules

test: bayes
	@echo "sandboxing test run: bayes"
	./bayes -v32 -r4096 -n10 -p40 -i2 -e8 -s1