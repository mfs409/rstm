TORULES := ..

SOURCES := dwyer.cpp edge.cpp edge_set.cpp mesh.cpp my_thread.cpp point.cpp \
           queues.cpp side.cpp worker.cpp

EXECS   := mesh

-include $(TORULES)/Makefile.rules

test: mesh
	@echo "sandboxing test run: mesh"
	./mesh -n1000000