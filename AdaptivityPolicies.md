# Introduction #

RSTM now supports machine-learning based adaptivity, in addition to static adaptivity policies.  Below we describe how these features currently work.

# Non-ML Adaptivity #
There are 4 adaptivity policies designed for pathology avoidance.  They vary based on whether they offer privatization safety (E, ER) or not (X, R).  They als vary based on whether they support self-abort (ER, R) or not (E, X).

These policies can be selected by providing their name as the parameter to `TM_SET_POLICY()`, or by providing their name in the STM\_CONFIG environment variable.

Note that while these policies do avoid pathological behavior (e.g., they avoid starvation and livelock), they do not give very good overall performance.  However, they provide a simple example that you can extend to build your own adaptivity policies.

# ML-Based Adaptivity #

## Important Note ##

While we have experimented with many techniques for machine learning (see Wang Transact 2011), the current release only has a small subset.  Current ML-based adaptivity is experimental.

## High-Level Description ##

The released ML adaptivity is based on a pattern-matching approach (a very simple form of case-based reasoning).  You can run off-line tests to characterize an application and approximate its behavior, save that information in a 'qtable', and then during program execution, measure a few transactions and compare them to the qtable to find a suitable algorithm.

## Debugging Mechanisms ##
Selecting the `PROFILE_NOCHANGE` policy will configure libstm such that NOrec is used, but some transactions are profiled during execution.

Selecting the `ProfileAppAvg`, `ProfileAppMax`, or `ProfileAppAll` runtimes will configure libstm such that all transactions are measured, and then either the average, max, or sum of measurements will be printed at the end of the application.  Note that these runtimes are only correct when there is exactly one transactional thread at any time.

## Features ##

We measure 8 high-level features, which are then used for making decisions:
  * Number of reads before the first write
  * Number of reads after the first write
  * Number of reads to locations that have been written (RAW)
  * Number of writes
  * Number of writes to locations that have been written (WAW)
  * Transaction duration
  * Interval between transactions
  * Read-Only Ratio

There are 31 pre-set combinations of these features that can be used to compare rows:
  * CBR\_RO
  * CBR\_Read
  * CBR\_Write
  * CBR\_Time
  * CBR\_RW
  * CBR\_R\_RO
  * CBR\_R\_Time
  * CBR\_W\_RO
  * CBR\_W\_Time
  * CBR\_Time\_RO
  * CBR\_R\_W\_RO
  * CBR\_R\_W\_Time
  * CBR\_R\_Time\_RO
  * CBR\_W\_Time\_RO
  * CBR\_R\_W\_Time\_RO
  * CBR\_TxnRatio
  * CBR\_TxnRatio\_R
  * CBR\_TxnRatio\_W
  * CBR\_TxnRatio\_RO
  * CBR\_TxnRatio\_Time
  * CBR\_TxnRatio\_RW
  * CBR\_TxnRatio\_R\_RO
  * CBR\_TxnRatio\_R\_Time
  * CBR\_TxnRatio\_W\_RO
  * CBR\_TxnRatio\_W\_Time
  * CBR\_TxnRatio\_RO\_Time
  * CBR\_TxnRatio\_RW\_RO
  * CBR\_TxnRatio\_RW\_Time
  * CBR\_TxnRatio\_R\_RO\_Time
  * CBR\_TxnRatio\_W\_RO\_Time
  * CBR\_TxnRatio\_RW\_RO\_Time

Note that these policies are not necessarily going to give good performance.  However, the code provides a clear demonstration of how ML-based policies work.  Better policies, based on different ML algorithms, are easy to implement in the framework.

## Using ML Adaptivity ##
There are two environment variables that you will need to set:
  * STM\_NUMPROFILES: the number of transactions to profile before making a decision
  * STM\_QTABLE: the location of a qtable file

An example script for creating a qtable can be found in the source code, as demo\_train\_cbr.pl.