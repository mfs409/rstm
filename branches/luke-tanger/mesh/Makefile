TORULES  := ../..
SOURCES  := dwyer.cpp edge.cpp edge_set.cpp mesh.cpp my_thread.cpp point.cpp \
            queues.cpp side.cpp worker.cpp
CXXFLAGS += -I.. -I../include -I$(TORULES)/../build/rstm/include
CXXFLAGS += -fno-exceptions
EXECS    := mesh

-include $(TORULES)/Makefile.rules

test: mesh
	@echo "sandboxing test run: mesh"
	./mesh -n1000000