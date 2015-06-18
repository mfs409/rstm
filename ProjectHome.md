### About RSTM ###
RSTM is one of the oldest open-source Software Transactional Memory libraries.  Since its first release in 2006, it has grown to include several distinct STM algorithms.  It also supports several architectures and operating systems (x86 / SPARC; Linux, Solaris, MacOS).

As a research system, not all configurations are currently supported.  However, among the various options one can find support for strong semantics (privatization, publication), irrevocability, condition synchronization (via 'retry'), and strong progress guarantees.

### New Release (July 6, 2011) ###

Version 7 is now available.  It features compatibility with the Intel STM compiler, several new algorithms, and support for plug-in adaptivity policies for choosing the best STM algorithm at runtime.  It also supports 32-bit and 64-bit code, and uses cmake to simplify configuration.

Please note that while there are available .tgz files, we strongly encourage you to use the subversion trunk to get the latest version of the code.

### Update (Aug 9, 2011) ###
There is a known issue with the ICC interface in recent revisions of the code.  If you use the subversion trunk to download RSTM, please use [revision 72](https://code.google.com/p/rstm/source/detail?r=72) until further notice.