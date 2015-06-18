# Introduction #

Most of RSTM's configuration is done at compile time.  The following environment variables enable run-time configuration:

  * STM\_CONFIG: The name of the STM algorithm to use, or the name of the run-time adaptivity policy to use.
  * STM\_QTABLE: The location of a qtable file to use when using ML-based adaptivity.
  * STM\_NUMPROFILES: The number of transactions to profile before making an adaptivity decision.

## Debugging ##
Use adaptivity to figure out if your code is the problem.  There is a big difference between code that crashes with CGL and code that only crahses with other STMs.  There is a big difference between code that crashes with Library API and code that crashes with CXXTM API.  You can adapt from outside of any transaction, so it's easy to change algorithms as needed to narrow the scope of your debugging search.

## Performance Tuning ##
If you use the ProfileAppAll STM and run your code in single-thread mode, you can generate the total number of reads/writes per transaction.  This provides the start of a method for comparing library instrumentation to compiler instrumentation, and figuring out if something is being over-instrumented.