# Introduction #

This page describes the applications that are included with RSTM.  If you create an application and would like it added to RSTM, please contact us.

## Microbenchmarks ##

The `bench` folder has several microbenchmarks.  In the past, these microbenchmarks were all part of a single executable.  We now build them separately, which makes it much easier to see how to create an individual TM-based application.

### Data Structure Microbenchmarks ###
These benchmarks are the traditional way of evaluating STM algorithms:
  * CounterBench: All threads increment a shared counter
  * ListBench: Threads insert/remove/lookup from a singly-linked list
  * DListBench: Threads insert/remove/lookup from a doubly-linked list
  * HashBench: Threads insert/remove/lookup in a hashtable that uses internal chaining
  * TreeBench: Threads insert/remove/lookup in a red-black tree
  * ForestBench: Like Treebench, but each transaction operates on multiple trees in a forest.
  * TreeOverwriteBench: Like TreeBench, but all transactions are writers that either insert or remove a value.

### Behavior Microbenchmarks ###
These benchmarks mirror behaviors that might be common in TM-based applications:
  * ReadWriteNBench: Each transaction reads N locations, and then writes N locations.
  * MCASBench: Transactions essentially perform a multi-word compare and swap, by reading and then writing N successive locations.
  * ReadNWrite1Bench: Transactions read N locations, and then write 1 location.

### Pathologies and Unit Tests ###
  * TypeTest: A simple test to ensure that the read/write instrumentation works correctly for all common types.
  * DisjointBench: Transactions do not conflict.  Useful for observing bottlenecks within an STM implementation.
  * WWPathologyBench: Transactions are likely to livelock and/or starve (Spear PPoPP 2009).

### Configuration Parameters ###
The microbenchmarks accept the following parameters.  Not all have meaning to all microbenchmarks:
  * `-d`: number of seconds to time (default 1)
  * `-X`: execute fixed tx count, not for a duration
  * `-p`: number of threads (default 1)
  * `-N`: nops between transactions (default 0)
  * `-R`: % lookup txns (remainder split ins/rmv)
  * `-m`: range of keys in data set
  * `-B`: name of benchmark
  * `-S`: number of sets to build (default 1)
  * `-O`: operations per transaction (default 1)
  * `-h`: print help
Note that arguments to `-B` are microbenchmark-specific, and can be found in the code.

## STAMP (Minh IISWC 2008) ##
We build STAMP using a C++ compiler.  This leads to a few minor changes to how comparators are passed to functions.  We include our patched STAMP in the release.

This patch also incorporates Aleksandar Dragojevic's patch for allowing non-power-of-2 thread levels.

For inputs to STAMP applications, please see the STAMP website.

Please note that "yada" does not work correctly.  It is broken in the original STAMP release, and we have not fixed it.

Also, note that labyrinth may not work for some of our algorithms, because it requres self-abort.  There is an easy fix for this problem.  If you are interested, please contact us.

### C++ STM Stamp ###

We have ported three of the STAMP benchmarks to use the C++ STM API, kmeans, ssca2, and vacation. This is achieved with some custom STAMP macro mapping in tm.h. This means that, if you have configured to `rstm_enable_itm` and/or `rstm_enable_itm2stm`, the build system will build versions of these apps using your C++ STM compiler and link to `libitm.a` and `libitm2stm.a libstm.a`, respectively.

This is described in further detail in our [TRANSACT '11 paper](http://www.cs.purdue.edu/transact11/web/papers/Kestor.pdf). Note that the released code is _not_ the code used to get performance results in that paper. The STAMP release is RSTM's modified version of STAMP, the shim has been entirely rewritten to support a much broader range of ITM behavior, and the underlying STM implementation has evolved dramatically since the RSTM v5 release that the paper was based on.

For the most part, porting a STAMP application requires that the entire subtree of transactionally called functions be labelled `TM_CALLABLE`. If you forget to label one of the functions, the compiler will silently insert a "become irrevocable" call, invalidating the performance that you obtain. You may find configuring `libitmstm.a` to `assert_on_irrevocable` to be a helpful debugging tool here. You may also find that making STAMP's `BEGIN_TRANSACTION`macro be `atomic` and the `TM_CALLABLE` annotation `[[transaction_safe]]` to be helpful here as well. Note however that STAMP uses function pointers inside of transactions, a behavior that `icc` does not currently support inside of `atomic` transactions---apparently statically typing a function pointer as `[[transaction_safe]]` is not yet enforced. This means that some transactions **must** be `relaxed`. In addition there are a few places where standard library calls are made transactionally, also unsupported in `atomic` transactions.

If you _do_ port additional applications let us know, we'd be happy to include your patch in the RSTM release if that's possible.

One further issue is the use of Intel's `__transaction [[waiver]]`. In some cases STAMP calls uninstrumented functions transactionally, even though they are not technically `tm_pure`. These are mainly the data-structure `compare` functions, and are generally in performance critical inner loops. We use `TM_WAIVER` to support such behavior in order to remain true to STAMP's original design, however we provide a configuration option to disable waivers. This provides the ability to examine the cost of waiver.

## Delaunay triangulation (Scott IISWC 2007) ##
We also include a Delaunay triangulation application, `mesh`. The benchmark is currently designed to generate a fixed number of points in the plane, and then uses a phase-based approach towards creating a Delaunay triangulation of this set of random points. These phases switch between shared and private access to the set of points.

There are currently three options for synchronizing the shared accesses. There is an explcit _coarse-grain lock_ implementation which protects the shared data with a single global pthread lock. There is a substantially more complicated, _fine-grain lock_ implementation which uses many small locks. Finally, if you have access to a C++ STM Draft standard compiler, such as [Intel's experimental STM version of `icc`](http://software.intel.com/en-us/articles/intel-c-stm-compiler-prototype-edition/) you can build using transactional memory. TM supports both Intel's included `libitm.a` as well as our `libitm2stm.a` shim, as selected during configuration. All four versions can be built at the same time.

The `mesh` application does not currently support 64-bit execution, so it will only be available if [you have selected](BuildingRSTM.md) to build 32-bit libraries and applications.

If requested the application will output a set of edges used to build the triangulation. We provide the `Display.java` viewer so that you can visualize this process.

### Parameters ###
  * ` -n`: number of random points to generate
  * ` -w`: number of worker threads to execute
  * ` -s`: initial random seed
  * ` -p`: read points from the input rather than random generation
  * `-oi`: output edges incrementally
  * `-oe`: output edges at end
  * ` -v`: output individual timings (implied by `-oi` and -oe`)