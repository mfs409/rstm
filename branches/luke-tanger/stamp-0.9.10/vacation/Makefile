TORULES := ../..

SOURCES  := ../lib/pair.c ../lib/mt19937ar.c ../lib/random.c ../lib/thread.c \
            ../lib/list.c ../lib/rbtree.c \
            client.c customer.c manager.c reservation.c vacation.c

CXXFLAGS += -I../lib
CXXFLAGS += -DLIST_NO_DUPLICATES -DMAP_USE_RBTREE

EXECS    := vacation

-include $(TORULES)/Makefile.rules

vacation-high: vacation
	@echo "sandboxing test run: vacation-high"
	./vacation -n4 -q60 -u90 -r1048576 -t4194304

vacation-low: vacation
	@echo "sandboxing test run: vacation-low"
	./vacation -n2 -q90 -u98 -r1048576 -t4194304

test: vacation-high vacation-low
