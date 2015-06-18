# Introduction #

The RSTM library supports several different underlying STM algorithms.  All algorithms are blocking, and all operate at the granularity of individual words, as opposed to language-level objects.  The broad classes of algorithms, as well as the specific algorithms, are described below.

Please note that for the most part, we do not refer to algorithms by their published names, unless they were first published as part of RSTM.  This decision is intended to make it clear that the published versions of those algorithms may have different behavior than ours, due to factors that are common throughout RSTM, but that are not common to other evaluation frameworks.  Examples of how RSTM implementations might differ from the algorithms upon which they are based include:
  * How orec logging is performed (see individual implementations for examples).
  * The write set implementation for redo-log algorithms (see WriteSet.hpp).
  * How memory management is done (see WBMMPolicy.hpp)

Note: All algorithms are privatization safe unless otherwise specified in the headings.

## Single-Lock Algorithms ##

These algorithms do not allow for concurrent execution of transactions.  However, they have very low single-thread latency.  In all cases, a single lock is acquired at transaction begin, and released at transaction commit.  Unless otherwise stated, these algorithms do not allow explicit self-abort.  These algorithms all offer privatization safety.

  * CGL: Uses a test-and-test-and-set lock with exponential backoff
  * Ticket: Uses a first-come first-served ticket lock, implemented with two counters.
  * MCS: Uses an MCS lock
  * Serial: CGL, except that writes are logged (undo logging) so that self abort is possible.

## Sequence-Lock Algorithms ##

These algorithms use a sequence lock as the only global metadata.  A sequence lock is odd when held, and even when available.  These algorithms all offer privatization safety.

  * TML: Optimized for workloads with lots of read-only transactions.  Transactions do not log their reads, and writes are performed in-place.  Whenever a transaction performs its first write, all concurrent transactions abort, and no new transactions start until the writer commits.  Self-abort of writing transactions is not allowed.  Details are available in (Dalessandro EuroPar 2010).

  * TMLLazy: Like TML, but writes are buffered until commit time.  This allows more concurrency, and supports self-abort.  However, there is still no concurrency among writers.

  * NOrec: Extends TMLLazy with value-based validation.  This means that when a writer commits, all concurrent transactions can check the actual values they read, in order to determine if they can continue operating or not.  Details are available in (Dalessandro PPoPP 2010).
    * NOrec Variants: There are several available variants of NOrec, based on the behavior taken upon abort.  The variants are NOrec (immediate restart on abort), NOrecHour (uses the Hourglass protocol (Liu Transact 2011)), NOrecBackoff (exponential backoff on abort), NOrecHB (Hourglass + backoff)

  * NOrecPrio: Extends NOrec with simple priority.  Transactions block at their commit point if there exist higher-priority in-flight transactions.

## Orec-Based Algorithms (Not Privatization Safe) ##
Ownership records, or "orecs", are used to detect conflicting accesses to memory.  These algorithms are not privatization safe.

  * LLT: Writes are buffered, and locks are acquired at commit time.  A timestamp is used to limit validation, using the GV1 algorithm.  This algorithm resembles TL2 (Dice DISC 2006).

  * OrecEager: Writes are performed in-place, and on abort, an undo log is used.  This algorithm resembles TinySTM (Felber PPoPP 2008) with write-through, except that we do not use incarnation numbers.
    * OrecEager Variants: OrecEager (immediate restart on abort), OrecEagerHour (Hourglass on abort), OrecEagerBackoff (exponential backoff on abort), OrecEagerHB (Hourglass + backoff)

  * OrecEagerRedo: Like OrecEager, but with redo logs (still using encounter-time locking).  Resembles TinySTM with write-back.

  * OrecLazy: Like OrecEager, except that commit-time locking is used.  This algorithm resembles the unpublished "CTL" version of TinySTM, as well as the "Patient" algorithm in (Spear PPoPP 2009).  However, this algorithm uses timestamps in the same manner as (Wang CGO 2007), unlike previously published lazy STM algorithms.
    * OrecLazy Variants: OrecLazy, OrecLazyHour, OrecLazyBackoff, OrecLazyHB

  * OrEAU: These are OrecEager variants extended to support remote abort of conflicting transactions.
    * OrEAU Variants: OrEAUBackoff (backoff on abort), OrEAUFCM (a timestamp mechanism to manage aborts), OrEAUNoBackoff (always abort other on conflict), OrEAUHour (use Hourglass protocol on abort)

  * OrecFair:  This is like OrecLazy, extended to support priority.  Transactions can gain priority, and then be guaranteed to win conflicts, while remaining lazy.  The algorithm most closely resembles one from (Spear PPoPP 2009).

  * Swiss: Like SwissTM (Dragojevic PLDI 2009).  The algorithm uses eager locking with redo logs, and a two-pass commit to implement mixed invalidation as in (Spear DISC 2006).

  * Nano: A locking version of WSTM (Fraser OOPSLA 2003).  There is **quadratic** validation overhead, and no global timestamp.  For frequent, tiny transactions, this algorithm is very good.  For larger workloads, it is much worse (asymptotically and in practice).

## Orec-Based Algorithms (Privatization Safe) ##

These algorithms use a "two-counter" technique and polling to add privatization safety.  The technique has been described in (Marathe ICPP 2008), (Spear OPODIS 2008), (Detlefs (unpublished)), and (Dice Transact 2009).

  * OrecELA: Extends LLT (TL2) to achieve ALA publication safety and privatization safety.

  * OrecALA: Extends OrecLazy to achieve only privatization safety.

## Signature-Based Algorithms ##

These algorithms use signatures (single-hash-function Bloom Filters) for concurrency control.

  * RingSW: The single-writer variant of RingSTM (Spear SPAA 2008), extended to support SSE instructions on the x86.

  * RingALA: Extends RingSW with an extra per-thread 'conflicts filter' to achieve ALA publication safety.

## Byte Lock Algorithms ##

These algorithms use bytelocks, as proposed in (Dice Transact 2009) and (Dice SPAA 2010).  Readers are visible, which avoids the need for validation.

  * ByteEager: Uses in-place update with undo logs and timeout-based conflict resolution.  Very similar to TLRW (Dice SPAA 2010).

  * ByteEagerRedo: Like ByteEager, but uses in-place update with redo logs.

  * ByEAU: Adds remote-abort support to ByteEager, so that conflicts can be resolved without timeout.  Note that this does not usually improve performance.
    * ByEAU Variants: ByEAUBackoff (backoff on abort), ByEAUFCM (timestamps for managing aborts), ByEAUNoBackoff (immediate restart on abort), ByEAUHour (Hourglass protocol on abort)

  * ByEAR: Adds remote-abort support to ByteEagerRedo.

  * ByteLazy: Like ByteEager, but with commit-time write locking and redo logs.  Note that read locks are still acquired eagerly.

## Bit Lock Algorithms ##

Bitlocks, as proposed in (Marathe Transact 2006), provide an alternative to bytelocks for implementing visible reads.

  * BitEager: Like ByteEager, but with Bitlocks.

  * BitEagerRedo: Like ByteEagerRedo, but with Bitlocks.

  * BitLazy: Like ByteLazy, but with Bitlocks.

## Invalidation Algorithms ##
With invalidation (Gottschlich CGO 2010), committing transactions forcibly abort conflicting transactions.

  * TLI: A version of InvalSTM that does not require the use of per-thread mutexes, instead favoring ordered writes of per-thread metadata and a sequence lock for commits.

## Globally Ordered Algorithms ##
These algorithms are based on work on thread-level speculation with transactions (Spear LCPC 2009).  Transactions are assigned a commit order very early (details vary by algorithm), and exploit order to reduce overheads.

  * Pipeline: Every transaction gets an order at begin time.  The 'oldest' transaction performs writes in-place, while other transactions buffer writes.  Note that self-abort is not allowed.

  * CTokenTurbo: Only writer transactions get an order, and they do so at the time of their first write.  Self-abort is not allowed by writer transactions.  The oldest transaction performs writes in-place.

  * CToken: Like CTokenTurbo, except that the oldest transaction still buffers its writes until commit time, so that self-abort can be supported.