BENCHMARKS := bayes genome intruder kmeans labyrinth ssca2 vacation yada

.PHONY: all clean $(BENCHMARKS)

all: $(BENCHMARKS)

$(BENCHMARKS):
	$(MAKE) -C $@

clean:
	@find . -name "*.bc"  | xargs rm
	@find . -name "*.ll"  | xargs rm
	@find . -name "*.o"   | xargs rm
	@find . -name "*.s"   | xargs rm
	@find . -name "*.dot" | xargs rm
	@find . -name "*.log" | xargs rm
	@rm -f $(foreach bench,$(BENCHMARKS),$(bench)/$(bench))
