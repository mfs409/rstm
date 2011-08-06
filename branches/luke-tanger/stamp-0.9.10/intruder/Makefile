TORULES := ../..

SOURCES  := ../lib/list.c ../lib/mt19937ar.c ../lib/pair.c ../lib/queue.c \
            ../lib/random.c ../lib/rbtree.c ../lib/thread.c ../lib/vector.c \
            decoder.c detector.c dictionary.c intruder.c packet.c \
            preprocessor.c stream.c

CXXFLAGS += -I../lib
CXXFLAGS += -DMAP_USE_RBTREE

EXECS    := intruder

-include $(TORULES)/Makefile.rules

test: intruder
	@echo "sandboxing test run: intruder"
	./intruder -a10 -l128 -n262144 -s1