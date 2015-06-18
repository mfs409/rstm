# Introduction #

RSTM supports two APIs.  The Library API is the legacy interface for RSTM.  On SPARC, it is currently the only supported interface.  On x86 with the Intel transactional C++ compiler, it is also possible to use the [C++ STM Draft API](http://groups.google.com/group/tm-languages/attach/199fda7f6cbf4e0d/C%2B%2B-transactional-constructs-1.0.pdf?part=4).  It is also possible to write code that works with both APIs.

## C++ STM API ##
If you are planning on using the Intel TM C++ compiler, you could just follow the rules in the draft C++ TM specification.  That is, you could:
  * wrap transactional regions with `__transaction[[atomic]] { ... }`
  * mark transactional functions as [[transaction\_safe](transaction_safe.md)]
  * mark regions that should not be instrumented with transaction [[waiver](waiver.md)] { ... }
  * place the proper calls to initalize and shut down the STM library, and each thread's STM context.

By adding an include of <api/api.hpp>, you will also gain the following:
  * `TM_SET_POLICY(P)` to set the algorithm or adaptivity policy during execution.
  * `TM_GET_ALGNAME()` to get the initial algorithm with which libstm was configured.

## Library API ##

The Library API allows maximum flexibility.  You can create new interfaces to libstm, and exploit them immediately.  However, it is also difficult.  With the library API, the programmer is responsible for making any shared memory access safe.  The programmer is also responsible for ensuring that allocation/reclamation are safe, and for adding API calls necessary for good performance.

Consider the following example, which demonstrates manual instrumentation of shared memory loads and stores:

```
  atomic {
    x++;
  }
```

With the Library API, this becomes:
```
  TM_BEGIN(atomic) {
    int z = TM_READ(x);
    TM_WRITE(x, z+1);
  } TM_END;
```

Note that `TM_BEGIN` should always be given the parameter `atomic`.

If your transaction calls `malloc` or `free`, you must use the transaction-safe variants: `TM_ALLOC` and `TM_FREE`.

Broadly speaking, you need to follow these additional rules:

  * Include <api/api.hpp> in your code.
  * Before any transactions, some thread must call `TM_SYS_INIT`, and after all transactions are done, a thread must call `TM_SYS_SHUTDOWN`.
  * A thread cannot perform transactions until it calls `TM_THREAD_INIT`.  It should call `TM_THREAD_SHUTDOWN` when it is done.
  * To forcibly change the current STM algorithm or adaptivity policy, use `TM_SET_POLICY(policyname)`, where policyname is either an algorithm name or an adaptive policy name.
  * To force a transaction to become irrevocable, use `TM_BECOME_IRREVOC()`.

Furthermore, you should be careful about the following:

  * `TM_READ()` and `TM_WRITE()` require a valid transaction descriptor.  You can either ensure that a call to `TM_GET_THREAD()` occurs in a visible lexical scope, or else pass the descriptor to functions using the STAMP-inspired `TM_ARG` / `TM_ARG_ALONE` / `TM_PARAM` / `TM_PARAM_ALONE` interface.

  * Since self-abort does not quite follow the spec (it is not `cancel`), we expose `stm::restart()` for this purpose.

  * To find the algorithm name with which the system was initialized, use `TM_GET_ALGNAME()`.  Note that this only returns the initial algorithm/policy configuration, not any changes due to adaptivity or explicit calls to `TM_SET_POLICY()`

There is a hack for fast nontransactional initialization of data structures, via `TM_BEGIN_FAST_INITIALIZATION` and `TM_END_FAST_INITIALIZATION`.  See the code for examples.

## Supporting Both APIs ##
If you program to the Library API, and then compile with the Intel C++ compiler, everything will _just work_ regardless of which API you use to configure and build.  There are a few caveats:

  * `TM_WAIVER`: If you know that a region should not be instrumented, then you need to place it in a "waiver" block.  To keep "waiver" compatible with the librar API, we use the macro `TX_WAIVER` instead of `__transaction [[waiver]]`.

  * Any function that is called from a transaction, and has shared memory accesses, must be marked [[transaction\_safe](transaction_safe.md)].  Using the `TM_CALLABLE` macro for this purpose ensures compatibility with the Library API.

  * You must make sure that code that requires irrevocability has a call to `TM_BECOME_IRREVOC()`