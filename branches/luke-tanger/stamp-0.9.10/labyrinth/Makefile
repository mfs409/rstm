TORULES  := ../../..
SOURCES  := ../lib/list.c ../lib/mt19937ar.c ../lib/pair.c ../lib/queue.c \
            ../lib/random.c ../lib/thread.c ../lib/vector.c \
            coordinate.c grid.c labyrinth.c maze.c router.c
CFLAGS   += -I../lib -I../.. -I../../include -I$(TORULES)/../build/rstm/include
CFLAGS   += -DUSE_EARLY_RELEASE
LLVM-GCC := llvm-g++
EXECS    := labyrinth

-include $(TORULES)/Makefile.rules

test: labyrinth
	@echo "sandboxing test run: labyrinth"
	./labyrinth -i inputs/random-x512-y512-z7-n512.txt