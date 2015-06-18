# Introduction #

This page describes how to configure and build RSTM.  There are two main targets:

  * Library API: This target is suitable for SPARC, and for x86 platforms that use gcc as a compiler.

  * CXXTM API: This target is suitable for x86 platforms that have the Intel transactional C++ compiler and is in addition to the Library API on such platforms.

If you are building to use the Library API, then you will need to build libstm.a.  If you are building to use the CXXTM API, then you will need to build both libstm.a and libitm2stm.a.  Cmake will help you do this easily.

Note that libitm2stm requires certain settings for libstm, which can introduce overheads (byte-granularity support, cancel-and-throw, stack protection) that can be avoided with the Library API.  However, with the Library API, it is the programmer's responsibility to correctly annotate every shared memory access within each transaction (particularly in order to avoid the need for these features!).  In cmake, if you enable the option for building libitm2stm, support for these features will be turned on automatically.

## CMake ##

As of the 7th release, RSTM uses the [cmake configuration infrastructure](http://www.cmake.org/) which is [available in binary form](http://www.cmake.org/http://www.cmake.org/cmake/resources/software.html) for all of the platforms that we currently support. While we do not knowingly use cmake features that are unsupported in the 2.8.0 release, we recommend downloading the latest stable release (2.8.4 at the time of the 7th release) as we have not extensively tested configuration with other cmake verions.

We have tested STM configurations on the following platforms.

  * Linux x86(`_`64)
  * Solaris x86(`_`64) and SPARC v9
  * Mac OS X 10.6 x86(`_`64)

We recommend out-of-source builds. As is standard you may create a directory and configure with the execution command `cmake <path-to-RSTM>`. This will configure RSTM with default parameters for your platform, using the Makefile generator. After configuration you can simply `make` and all of the default libraries and executables will be generated.

```
  unpack rstm to /home/me/rstm_src
  mkdir /home/me/rstm_build
  cd /home/me/rstm_build
  cmake ../rstm_src
  make
```

**NOTE: We do not recommend using the 'make install' command**

You have three options for customizing your configuration:
  1. You may do an _interactive configuration_ using cmake's `-i` flag. This will prompt you for answers to all of the possible configuration options. Be aware that some options are marked as _advanced_ and thus will only be visible if you say `yes` to cmake's interactive query to show advanced variables. We recommend against setting advanced configuration parameters interactively.
  1. You may explicitly set options on the command line using cmake's `-D` command line functionality. See [cmake's documentation for more details](http://www.cmake.org/). As an example, to build 32-bit libraries and executables on a platform where 64-bit is the default, you could directly set the `rstm_build_32-bit` option on the command line like `cmake <path-to-RSTM> -Drstm_build_32-bit:BOOL=YES`.
  1. You may configure using a cmake gui, such as the `ccmake` ncurses gui included in the cmake distribution. We recommend this for browsing and setting advanced configuration parameters, which can be accessed by toggling the view to advanced.

Note that our configurations are adaptive in the sense that enabling certain options may add additional options to your gui. An example is enabling 32-bit builds on Mac OS X, which enables the `mesh` benchmarks and related mesh options.

For developers, user configuration options are declared in `UserConfig.cmake` files in the appropriate directories. For example, configuration options for `libstm` are defined in `libstm/UserConfig.cmake`.

## CMake Options ##

This describes the cmake configuration options as of RSTM's 7th release. Note that there are some dependencies between these options, so some may not be visible on your platform given the rest of your configuration settings.

### RSTM Options ###

RSTM options are defined in `<RSTM>/UserConfig.cmake`. These options control the RSTM configuration itself.

  * `rstm_build_32-bit`: RSTM has been designed to support simultaneous 32 and 64-bit builds on platforms where it is applicable. This option enables 32-bit libraries and applications. All 32-bit targets are appended with the string `32`. As an example, the 32-bit version of libstm is titled `libstm32.a`. The default architecture is determined by your C++ compiler's default `sizeof(void*)`.

  * `rstm_build_64-bit`: see `rstm_build_32-bit`

  * `rstm_enable_itm2stm`: ON enables the itm2stm shim library. This shim is built using the standard C++ compiler on your system, so this option is available even on platforms that have no official ITM ABI compatible compiler.

  * `rstm_enable_itm`: ON enables executable builds that link directly with Intel's built-in `libitm.a` library. This option is only available if cmake finds a C++ STM compatible compiler who's ID matches Intel.

  * `rstm_enable_bench`: ON enables the RSTM bench set of microbenchmarks. The actual benchmarks that are built depend on your selections for using 32 and/or 64 bits, as well as your selection for `rstm_enable_itm2stm` and `rstm_enable_itm` (in the case where Intel's C++ STM compiler was detected).

  * `rstm_enable_stamp`: ON builds the distributed stamp benchmarks. 32 and/or 64 bit versions will be built depending on your configuration.

  * `rstm_enable_mesh`: ON builds the mesh benchmark. We currently only support 32 bit mesh builds. Executables will be generated based on your selection of `rstm_enable_itm` and `rstm_enable_itm2stm`.

### libstm Options ###

libstm options are defined in `<RSTM>/libstm/UserConfig.cmake`. These options control the libstm build and are embedded in the configured `include/stm/config.h` header so that libstm-dependent libraries and applications know how the library was configured.

  * `libstm_use_pthread_tls`: ON uses `pthread_(get|set)_specific` to store thread local data in the library. OFF defers to your C++ compiler's built-in thread local storage implementation (`__thread` on gcc). Mac OS X only supports pthread local storage, so Mac OS X users won't see this option.

  * `libstm_adaptation_points`: RSTM is now able to adapt between STM algorithms at runtime using the `stm::set_policy(STRING_NAME)` library call. The library is built with the ability to attempt to adapt automatically. This enumeration option tells the library when it should attempt to adapt. _none_ means never. _all_ means at transaction begin, commit, and abort barriers. _begin-abort_ does not attempt to adapt at commit barriers.

  * `libstm_enable_abort_histogram`: When implementing a new algorithm or Contention Manager it can be useful to get a histogram that shows how many toxic transactions occur. ON enables this statistics collection and output.

  * `libstm_use_sse`: On enables SSE instructions for Bloom Filter operations. This can increase the efficiency of the RingSW and RingALA implementations, but should not be used when the application itself is using SSE registers as we make no attempt to correctly checkpoint and restore the SSE subsystem.

  * `libstm_enable_byte_logging`: libstm naturally logs data at aligned word granulaties. The C++ STM Draft Standard however says that bytes are the fundamental access granularity, which means that transactional and non-transactional accesses to adjacent bytes in a word are not considered data-races and should not produce results that violate sequential consistency. libstm's standard word logging does not correctly support such a setting, thus we have extended all of the supplied STM algorithms with byte-logging capabilities. This comes with non-negligible overheads so we provide this configuration option. **Byte logging is required to build libitm2stm so this option is forced on when `rstm_enable_itm2stm` is selected**.

  * `libstm_enable_norec_false_conflicts`: One of NOrec's strong properties is its lack of false conflicts. When byte logging is enabled, we have the option to continue to provide this property by logging byte granularity accesses in the read log. This is expensive both in terms of read-log space and validation overhead, so we provide this option to allow you to allow false conflicts in NOrec in exchange for avoiding these overheads.

  * `libstm_enable_stack_protection`: If an STM logs writes to stack locations while executing there is the potential that redo or undo operations could perform destructive stack writes resulting in undefined behavior including arbitrary code execution. Users of the library API may be able to avoid stack instrumentation thus this option is provided. **This option is required by libitm2stm and is thus forced ON if `rstm_enable_itm2stm` is selected.**

  * `libstm_enable_cancel_and_throw`: The [C++ STM Draft standard](http://groups.google.com/group/tm-languages/attach/1dc8dbc595a02920/TMSpec-04-15-2010.pdf?part=4&view=1) permits _cancel-and-throw_ semantics, where a user can throw an exception out of a transaction that also aborts the transaction. In such circumstances writes to the exception object itself must be visible externally, after the transaction aborts. This option enables the library infrastructure to support this behavior. **This option is required by libitm2stm and is thus forced ON if `rstm_enable_itm2stm` is enabled.**

### libitm2stm Options ###

These options are defined in `<RSTM>/libitm2stm/UserConfig.cmake` and control how the libitm2stm build is performed. [The C++ STM Draft specification](http://groups.google.com/group/tm-languages/attach/1dc8dbc595a02920/TMSpec-04-15-2010.pdf?part=4&view=1) has more information about the terminology used here. **Note that these are currently only available in the svn repository.**

  * `itm2stm_enable_assert_on_irrevocable`: There are some situations when using the experimental [Intel C++ STM Compiler](http://software.intel.com/en-us/articles/intel-c-stm-compiler-prototype-edition/) where a nominally `[[atomic]]` transaction must be labelled `[[relaxed]]`. As an example, the specification implies that transactional function pointers (`[[transaction_safe]] void (*f)()`) should be strongly typed and statically enforceable and thus we should be able to use them in `[[atomic]]` transactions. The current prototype compiler does not enforce this and thus the use of a function pointer in an `[[atomic]]` transaction results in an error (you _can_ however use a member function which can provide an alternative in some cases). The use of `[[relaxed]]` transactions however means that if you call a function that is not marked `[[transaction_callable/safe]]` the compiler will generate a call to `changeTransactionMode` to force the transaction to become irrevocable without always providing a warning. This will quietly limit your scalability. Using the `assert_on_irrevocable` configuration can be a good performing debugging tool for this situation.

### bench Options ###

Bench options are defined in `<RSTM>/bench/UserConfig.cmake` and control how the bench build occurs.

  * `bench_enable_single_source`: Each benchmark can be built as a single source build, which provides the compiler the best opportunity to optimize the results. This can result in lower latency than multi-source execution. Single-source builds have SSB appended to their names.

  * `bench_enable_multi_source`: libitm and libitm2stm builds of the bench suite use multi-source builds. This option forces the libstm build to use this same strategy and is appropriate for comparisons with the C++ TM compiler builds. Multi-source executables have MSB appended to their names. The libitm and libitm2stm builds are always multi-source.

### STAMP Options ###

The STAMP options are defined in `<RSTM>/stamp-0.9.10/UserConfig.cmake` and control how the C++ STM build occurs.

  * `stamp_use_waiver`: This defines STAMP's `TM_WAIVER` macro to be `__transaction [[waiver]]` and can only be used with a compiler that supports Intel's waiver extension. It is used in the C++ STM build to call uninstrumented comparators, which is how STAMP was originally designed. You may disable waiver to see how the `__transaction [[waiver]]` extension aids performance in these benchmarks.

### mesh Options ###

Mesh options are defined in `<RSTM>/mesh/UserConfig.cmake` and control which versions of the mesh application to build. Mesh currently only supports 32-bit execution, so this option is only available if you have selected `rstm_build_32-bit`. Mesh will always be built with an libitm and libitm2stm version depending on your selections for `rstm_enable_itm` and `rstm_enable_itm2stm`.

  * `mesh_enable_cgl`: This option enables a hard-coded _coarse grain locking_ version of mesh, where transactional begin and end macros are defined to acquire and release a single global `pthread` lock.

  * `mesh_enable_fgl`: This options enables a hard-coded _fined grain locking_ version of mesh. The code path is substantially different than the `cgl` or C++ STM version.

  * `mesh_libstdcxx-v3_include`: The gcc-4.4 and higher libstdc++ headers make extensive use of variadic templates, which are unsupported by Intel's current C++ STM compiler. In order to use C++ standard library types and routines gcc-4.3 headers should be available. If these are not in your default include path, then you should download the libstdc++ included in gcc-4.3 and set this string option to be the path to the include directory.