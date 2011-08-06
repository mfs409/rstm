TORULES  := ../..

SOURCES  := ../lib/avltree.c ../lib/heap.c ../lib/list.c ../lib/mt19937ar.c \
            ../lib/pair.c ../lib/queue.c ../lib/random.c ../lib/rbtree.c \
            ../lib/thread.c ../lib/vector.c \
            coordinate.c element.c mesh.c region.c yada.c

CXXFLAGS += -I../lib
CXXFLAGS += -DLIST_NO_DUPLICATES -DMAP_USE_AVLTREE -DSET_USE_RBTREE

EXECS    := yada

-include $(TORULES)/Makefile.rules

test: yada
	@echo "sandboxing test run: yada"
	./yada -a15 -i inputs/ttimeu1000000.2