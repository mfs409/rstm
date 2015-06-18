# Introduction #

We are aware of a few places where the current RSTM code may act in surprising ways, or refuse to compile in surprising ways.  They are listed below.

## Compiling on SPARC ##

We currently only support GNU C++ on SPARC.  You will surely find references to SunCC in the code, and in fact, compiling with SunCC might just work, but we are not confident in our SunCC-specific code right now.  If you can help us, please send a patch!

Additionally, there are endian issues for some of our union code on SPARC.  What this means in practice is that you can't build on SPARC using byte-level logging... you have to use word-level logging.  Since libitm2stm is not supported on SPARC anyway, this shouldn't be a huge problem.

## Compiling Mesh ##

The Mesh code only compiles in 32-bit mode.

## Implicit Run-Time Thread Limits ##

Running more than 256 threads is not supported.  In addition, creating more than 256 transactional threads is not supported.  If your architecture does not have >256 threads, please consider using thread pools instead of creating and destroying threads.  If your architecture does have >256 threads, we believe that a simple change to a single #define will enable higher thread counts (at the cost of more overhead for some algorithms), but we haven't tested it.

Algorithms using ByteLocks do not support running with more than 56 threads.

## Using the Intel compiler with redo-based STM ##

  * Transaction local stack accesses
The rstm\_v7\_201107.tgz release file does not properly handle transaction local stack accesses for buffered-update STM algorithms. A workaround exists. See ITMSupport for more details. **This has been resolved in the [svn repository](http://code.google.com/p/rstm/source/checkout) and should no longer be a concern.**

  * Memory management
Certain, non-transactional uses of `munmap` may cause concurrent speculative transactions to suffer from segmentation faults that are inconsistent with single-lock execution. If you _must_ use `mmap/munmap` directly it is your responsibility to ensure that no concurrent speculative transactions  exist. You may do this by performing all `munmap` operations in `[[relaxed]]` transactions. Other memory management operations should work correctly. A more general fix for this is being developed.

## Irrevocability ##

RSTM now allows for threads to become irrevocable via aborting and restarting.  Though this fact has never been published, it seems to be true that in any system that (a) provides ALA publication safety; and (b) relies on per-location metadata (orecs, signatures, etc), this is the **only** safe way to do irrevocability.

Most support for switching to irrevocable mode in-flight has been removed, as it was out of date.  We left support in NOrec and OrecEager, just for demonstrative purposes.  More importantly, when in-flight switching fails in NOrec or OrecEager, the system reverts to abort-and-restart-irrevocably.  If you want to add support for in-flight irrevocability, please send a patch!

Also, please note that we have not extensively tested support for irrevocability.  If you discover bugs, please be sure to let us know.

## Self-Abort ##

Self-abort support is in flux right now.  The draft C++ TM specification favors cancelling transactions to self-aborting them.  Our 'restart' support has not been tested in a while, and 'retry' is not in the current code either.  We intend to fix these issues eventually, in order to improve standards compliance.  If you get to it first, please submit a patch.