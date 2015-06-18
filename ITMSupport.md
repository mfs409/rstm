# ITM Support In RSTM #

The 7th release of RSTM includes experimental support for [Intel's TM ABI](http://software.intel.com/en-us/articles/intel-c-stm-compiler-prototype-edition/#ABI) which allows RSTM, when properly configured, to be linked into an application compiled using [Intel's prototype C++ STM Compiler](http://software.intel.com/en-us/articles/intel-c-stm-compiler-prototype-edition/).

We use a _shim library_ approach to provide this support, based loosely on the description published in [TRANSACT'11](http://www.cs.purdue.edu/transact11/web/papers/Kestor.pdf). The shim library can be enabled with the cmake `rstm_enable_itm2stm` configuration option (see [BuildingRSTM](BuildingRSTM.md) for more details on cmake configuration). This will force certain configuration parameters in the `libstm.a` build to be ITM compatible, including support for byte logging, _cancel-and-throw_, and stack protection. It will also enable the build of the shim library itself, `libitm2stm.a`.

The build of `libitm2stm.a` is in no way dependent on the Intel compiler itself, it is simply a standard library built with your configured C++ compiler (likely `gcc`). Once you have built `libitm2stm.a` and `libstm.a` you can simply link `icc-tm` generated object files with your favorite compiler, passing the two RSTM libraries with -l (or using the full path to the `.a`s, sometimes a safer option).

Consider a simplified example for the commands we use to build the CounterBenchSHIM64 benchmark (SHIM indicates that this is using the shim library, as opposed to just `libstm.a` or Intel's `libitm.a`, and the 64 means that this is a 64-bit build):

```

g++ -I/<RSTM>/include -include itm/itm.h -m64 -o bmharness.o -c bmharness.cpp
icpc -Qtm_enabled -m64 -o DListBench.o -c -x c++ DListBench.cxxtm
g++ -m64 -L/intel/Compiler/11.0/610/lib/intel64 bmharness.o DListBench.o -o DListBenchSHIM64 -rdynamic -lirc \
../libitm2stm/libitm2stm64.a -lpthread ../libstm/libstm64.a -lrt -Wl,-rpath,/intel/Compiler/11.0/610/lib/intel64```

There are a number of useful things to note here.

  1. We mix two compilers here, using g++ to compile non-C++ STM code and icc to compile the C++ STM code, and using g++ to link. There is no problem doing this, except that Intel's compiler will implicitly use symbols from its own `-lirc` library.  Intel's `iccvars` script explicitly embeds either the 64-bit or 32-bit paths to this library in your environment, however we want to be independent of this so that `-m32` and `-m64` always work. For this reason we are adding the correct path as a `-L` and `-Wl,-rpath` flag.
  1. On Linux `libstm.a` needs `-lrt`.
  1. The RSTM libraries have the "64" suffix because this is a 64-bit build.
  1. `libitm64.a` and `libitm2stm64.a` are fully specified. They could be `-l` instead, this is simply a cmake decision.
  1. We will generally (and arbitrarily) use the `.cxxtm` type for C++ files that use the C++ STM API. We have to tell `icc` that these are C++ file though, hence the `-x c++` flags.
  1. We're including our version of Intel's `itm.h` in gcc. This is because we need to call `_ITM_(initialize|finalize)(Thread|Process)` directly (it's not part of the C++ STM API, but we can't include Intel's `itm.h` because it claims to be `icc`-only.
  1. **`libitm2stm.a` must appear before `libstm.a` on the link line.** This is a consequence of the "shim" design decision; `libitm2stm.a` maps ITM ABI calls into `libstm.a` calls, which are then resolved statically.

## Fully Implemented Features ##

We have implemented most of the functionality required by the [ITM ABI](http://software.intel.com/en-us/articles/intel-c-stm-compiler-prototype-edition/#ABI) for **all** of the RSTM library implementations. This includes the following features.

  * Byte granularity logging.
    * Both redo and undo log implementations operate at the byte level using RSTM's standard word-size logs augmented with byte masks.
  * _cancel-and-throw_ exception objects.
    * ITM registers exception objects and `libstm.a` will either avoid performing undo operations to it, or **actually perform** redo operations to it, depending on the underlying stm implementation.
    * Note that the [Single Lock algorithms](AvailableAlgorithms.md) cannot abort and thus do not technically support this operation.
  * Irrevocability.
    * The [Single Lock algorithms](AvailableAlgorithms.md) are naturally irrevocable.
    * Most of the algorithms implement irrevocability through an _abort-and-restart-as-irrevocable_ mechanism.
    * NOrec uses a more complicated in-flight irrevocability switch.
  * Transactional library calls.
    * Memcpy, memset, etc. are implemented in terms of RSTM's logging operations.
  * User-registered abort and commit handlers.
  * Transactional memory allocation.
    * This is not part of the ABI, but we forward transactional calls to malloc and free to RSTM's memory allocation subsystem.
    * You will need to manually tell `icc` that `malloc` and `free` are transactional.
```
extern "C" {
    [[transaction_safe]] void* malloc(size_t) __THROW;
    [[transaction_safe]] void free(void*) __THROW;
}
```

## Partially Supported Features ##

  * **Single Lock Semantics**
    * Both the [C++ STM API Draft](http://software.intel.com/en-us/articles/intel-c-stm-compiler-prototype-edition/#Constructs) and the [Intel ITM ABI](http://software.intel.com/en-us/articles/intel-c-stm-compiler-prototype-edition/#ABI) require that a TM implementation provide Single-Lock semantics. For C++ this reduces to privatization safety. **Some of the RSTM algorithms are not privatization safe.**
    * See the [list of available algorithms](AvailableAlgorithms.md) for more details on this point.
    * Switching to an algorithm that is not privatization safe will print a warning to your error stream.
  * Stack protection.
    * **Note that this has been fixed in the svn release. No special treatment for transaction local stack locations is necessary.**
    * We fully support uninstrumented stack writes for in-place algorithms such as the undo-log based implementations and the [Single Lock implementations](AvailableAlgorithms.md).
    * We do not fully support uninstrumented stack writes for redo-log implementations. `icc` may choose to elide instrumentation for local stack addresses even when these stack addresses are later passed to a function where they _are_ instrumented. Future read-after-write operations will not "see" redo-log writes. To avoid this issue you can force the compiler to instrument local stack writes by accesses the locations in question through volatile pointers.
  * Dynamic nesting.
    * RSTM does not yet provide the closed nesting support required to abort inner transactions in a user-visible way (i.e., we do not support the C++ STM Draft's `__cancel` construct from a nested context).

## Missing Features ##

  * `_ITM_tryCommitTransaction` and `_ITM_commitTransactionToID`
    * These appear to be used to implement certain types of exception handling.
  * SSE use
    * We log SSE data correctly, but we do not checkpoint or restore SSE registers necessary to correctly abort transactions. Any transaction that uses SSE should select one of the [Single Lock algorithms](AvailableAlgorithms.md) that does not support `__cancel` before execution.

## Known Issues ##

  * Transactions in `main()`
    * If you attempt to initialize the TM system in `main` but also have a top-level transaction `icc` will insert a call to `_ITM_getTransaction` just after the `main` prolog but before your initialization calls. The shim does not support this behavior (it's ok to skip the thread initialization call, but not the process initialization call). It's unclear if this is a bug in the shim, a bug in the ITM ABI which is unclear (in our opinion) on this point, or a bug in the compiler. As a workaround you can either not have a top-level transaction in `main`, or use a `__attribute__((constructor))` routine to initialize the process with `_ITM_initializeProcess`.

## Debugging advice ##

You will undoubtedly encounter bugs in your use of the shim.

The first thing you want to do is to link with Intel's `libitm.a` to figure out what the expected bahavior is (note that if you are linking `libitm.a` with `gcc` you need to set the correct `-L` and `-Wl,-rpath` as well as adding `-ldl` as a dynamic library to link with.

Once you have verified that your code _should_ work, you can check to see if it works using various RSTM algorithms, particularly the CGL algorithm. You can select an algorithm by setting your envionment's `STM_CONFIG` variable to the algorithm's name (i.e., CGL). CGL is both extremely safe, and relatively easy to debug with (it's just single-lock code). You might also try checking a few other algorithms, such as in-place, buffered, visible reader, and value-based validation options.

If you determine that the bug is only when you execute with a particular algorithm, you will want to narrow down the range of static transactions that may be causing your problem. The `include/stm/lib_globals.hpp` header is your friend here as it declares RSTM's adaptation interface, `stm::set_policy`. You can include this header from your code, and manually switch between algorithms at will. The resulting symbol for `stm::set_policy` will be defined in the `libstm.a` file that you are linking with, bypassing the shim. You can set the policy to be CGL, and then switch right before you run the failing transaction, and then switch back, and see if the bug manifests. One thing to note is that debugging the buffered TM implementations is difficult, as the "actual" values for variables are stored in the write log. You can accesses your thread-local write log in a debbugger through `_ITM_getTransaction()->thread_local_.writes`.

We have had trouble debugging any object files produced with optimizations enabled using Intel's experimental compiler. Make sure you use the `-g -debug all` flags and not -OX where X > 0 or -xSSEanything while debugging.

# C++ TM Executables #

The primary way that you will interact with the shim library is by writing executables that use the C++ STM Draft Standard (available from Intel at http://software.intel.com/en-us/articles/intel-c-stm-compiler-prototype-edition/#Constructs). The mesh application we provide is a good example of using this API (slightly obfuscated by the macros we use to support CGL and FGL builds in addition to the C++ STM build). The bench benchmarks are also a resource, however their programming interface is complicated by our desire to support a number of different APIs simultaneously.