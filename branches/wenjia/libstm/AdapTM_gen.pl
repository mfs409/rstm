<<<<<<< .mine
#!/usr/bin/perl

print "
 /**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */";

print " 
#include <stdint.h>
#include <iostream>
#include <cstdlib>
#include <setjmp.h> // factor this out into the API?
#include \"tx.hpp\"
#include \"platform.hpp\"
#include \"locks.hpp\"
#include \"metadata.hpp\"
#include \"adaptivity.hpp\"
#include \"alg.hpp\"
";

print "
/**
 * This STM is implemented differently than all others.  We don't give it a
 * custom namespace, and we don't rely on tx.cpp.  Instead, we implement
 * everything directly within this file.  In that way, we can get all of our
 * adaptivity hooks to work correctly.
 *
 * NB: right now, we pick an algorithm at begin time, but we don't actually
 *     adapt yet
 */
";

print "
namespace stm
{
  /**
   *  When the transactional system gets shut down, we call this to dump
   *  stats for all threads
   */
  void tm_sys_shutdown()
  {
      static volatile unsigned int mtx = 0;
      // while (!bcas32(&mtx, 0u, 1u)) { }
      for (uint32_t i = 0; i < threadcount.val; i++) {
          std::cout << \"Thread: \"       << threads[i]->id
                    << \"; RO Commits: \" << threads[i]->commits_ro
                    << \"; RW Commits: \" << threads[i]->commits_rw
                    << \"; Aborts: \"     << threads[i]->aborts
                    << std::endl;
      }
      CFENCE;
      mtx = 0;
  }

  /**
   *  To initialize the thread's TM support, we need only ensure it has a
   *  descriptor.
   */
  void tm_thread_init()
  {
      // multiple inits from one thread do not cause trouble
      if (Self) return;

      // create a TxThread and save it in thread-local storage
      Self = new TX();
  }

  /**
   *  When a thread is done using the TM, we don't need to do anything
   *  special.
   */
  void tm_thread_shutdown() { }

  /**
   *  All behaviors are reached via function pointers.  This allows us to
   *  change on the fly:
   */
  scope_t* (*rollback_)(TX* tx);
  void (*tm_begin_)(void*);
  void (*tm_end_)();
  const char* (*tm_getalgname_)();
  void* (*tm_alloc_)(size_t s);
  void (*tm_free_)(void* p);
  void* (* TM_FASTCALL tm_read_)(void** addr);
  void (* TM_FASTCALL tm_write_)(void** addr, void* val);

  /**
   *  The default mechanism that libstm uses for an abort. An API environment
   *  may also provide its own abort mechanism (see itm2stm for an example of
   *  how the itm shim does this).
   *
   *  This is ugly because rollback has a configuration-dependent signature.
   */
  NOINLINE
  NORETURN
  void tm_abort(TX* tx)
  {
      jmp_buf* scope = (jmp_buf*)rollback_(tx);
      // need to null out the scope
      longjmp(*scope, 1);
  }

  // for CM
  pad_word_t fcm_timestamp = {0};
  pad_word_t epochs[MAX_THREADS] = {{0}};
  TM_NAMES alg_index;
  // forward all calls to the function pointers
/*
  void tm_begin(void* buf) { tm_begin_(buf); }
  void tm_end() { tm_end_(); }
  void* tm_alloc(size_t s) { return tm_alloc_(s); }
  void tm_free(void* p) { tm_free_(p); }
  TM_FASTCALL
  void* tm_read(void** addr) { return tm_read_(addr); }
  TM_FASTCALL
  void tm_write(void** addr, void* val) { tm_write_(addr, val); }
*/

";

print "
  void tm_begin(void * buf)
  {
     printf(\"alg_index = %d\", alg_index);
     if(alg_index >= 0)
     {
         printf(\"running tm_begin of alg_index %d\", alg_index);
	 switch((TM_NAMES)alg_index)
	 {
";

@argument = @ARGV;
pop(@argument);
foreach $_ (@argument) {	
	print "\tcase $_:\n";
	s/([A-Z])/\U$1/gi;
	print "\t\tstm::${_}Begin(buf);\n";
	print "\t\tbreak;\n";
}

print "
		case AdapTM:
			printf(\"adaptm's tm_begin\\n\");
			stm::tm_begin(buf);
			break;

		default:
			assert(0);
			break;
         };
     }else
     { assert(0); tm_begin_(buf); };
  }
";

print "
  void tm_end()
  {
	printf(\"alg_index = %d\", alg_index);
    	if(alg_index >= 0)
  	{
       		printf(\"running tm_end of alg_index %d\", alg_index);
	 	switch((TM_NAMES)alg_index)
	 	{
";

@argument = @ARGV;
pop(@argument);
foreach $_ (@argument) {	
	print "\tcase $_:\n";
	s/([A-Z])/\U$1/gi;
	print "\t\tstm::${_}End(buf);\n";
	print "\t\tbreak;\n";
}

print "
		case AdapTM:
			printf(\"adaptm's tm_end\\n\");
			stm::tm_end();
			break;
		default:
			assert(0);
			break;
         	};

    	}else
    	{ assert(0); tm_end_(); }
  }
  
  void* tm_alloc(size_t s)
  {
	printf(\"alg_index = %d\", alg_index);
	if(alg_index >= 0)
    	{
        	printf(\"running tm_alloc of alg_index %d\", alg_index);
	 	switch((TM_NAMES)alg_index)
	 	{
";

@argument = @ARGV;
pop(@argument);
foreach $_ (@argument) {	
	print "\tcase $_:\n";
	s/([A-Z])/\U$1/gi;
	print "\t\tstm::${_}Alloc(s);\n";
	print "\t\tbreak;\n";
}

print "
		case AdapTM:
			printf(\"adaptm's tm_alloc\\n\");
			stm::tm_alloc(s);
			break;
		default:
			assert(0);
			break;
         	};
	}else
    	{ assert(0); return tm_alloc_(s); }
  }

  void tm_free(void* p)
  {
	printf(\"alg_index = %d\", alg_index);
    	if(alg_index >= 0)
    	{
      		printf(\"running tm_free of alg_index %d\", alg_index);
		switch((TM_NAMES)alg_index)
	 	{
";

@argument = @ARGV;
pop(@argument);
foreach $_ (@argument) {	
	print "\tcase $_:\n";
	s/([A-Z])/\U$1/gi;
	print "\t\tstm::${_}Free(p);\n";
	print "\t\tbreak;\n";
}

print "
		case AdapTM:
			printf(\"adaptm's tm_free\\n\");
			stm::tm_free(p);
			break;
		default:
			assert(0);
			break;
         	};
    	}else
    	{ assert(0); tm_free_(p); } 
  }
  
  TM_FASTCALL
  void* tm_read(void** addr) 
  {
	printf(\"alg_index = %d\", alg_index);
	if(alg_index >= 0)
	{
		printf(\"running tm_read of alg_index %d\", alg_index);
		switch((TM_NAMES)alg_index)
	 	{
";

@argument = @ARGV;
pop(@argument);
foreach $_ (@argument) {	
	print "\tcase $_:\n";
	s/([A-Z])/\U$1/gi;
	print "\t\tstm::${_}Read(addr);\n";
	print "\t\tbreak;\n";
}

print "
		case AdapTM:
			printf(\"adaptm's tm_read\\n\");
			stm::tm_read(addr);
			break;
		default:
			assert(0);
			break;
         	};
	}else
	{ assert(0); return tm_read_(addr); }
  }

  TM_FASTCALL
  void tm_write(void** addr, void* val)
  {
	printf(\"alg_index = %d\", alg_index);
 	if(alg_index >= 0)
	{
		printf(\"running tm_write of alg_index %d\", alg_index);
		switch((TM_NAMES)alg_index)
	 	{
";

@argument = @ARGV;
pop(@argument);
foreach $_ (@argument) {	
	print "\tcase $_:\n";
	s/([A-Z])/\U$1/gi;
	print "\t\tstm::${_}Write(addr, val);\n";
	print "\t\tbreak;\n";
}

print "
		case AdapTM:
			printf(\"adaptm's tm_write\\n\");
			stm::tm_write(addr, val);
			break;
		default:
			assert(0);
			break;
         	};
	}else
	{ assert(0); tm_write_(addr, val); }
  } 
";

print "
  /**
   *  Template Metapro:qgramming trick for initializing all STM algorithms.
   *
   *  This is either a very gross trick, or a very cool one.  We have ALG_MAX
   *  algorithms, and they all need to be initialized.  Each has a unique
   *  identifying integer, and each is initialized by calling an instantiation
   *  of initTM<> with that integer.
   *
   *  Rather than call each function through a line of code, we use a
   *  tail-recursive template: When we call MetaInitializer<0>.init(), it will
   *  recursively call itself for every X, where 0 <= X < ALG_MAX.  Since
   *  MetaInitializer<X>::init() calls initTM<X> before recursing, this
   *  instantiates and calls the appropriate initTM function.  Thus we
   *  correctly call all initialization functions.
   *
   *  Furthermore, since the code is tail-recursive, at -O3 g++ will inline all
   *  the initTM calls right into the sys_init function.  While the code is not
   *  performance critical, it's still nice to avoid the overhead.
   */
  template <int I = 0>
  struct MetaInitializer
  {
      /*** default case: init the Ith tm, then recurse to I+1 */
      static void init()
      {
          initTM<(TM_NAMES)I>();
          MetaInitializer<(stm::TM_NAMES)I+1>::init();
      }
  };
  template <>
  struct MetaInitializer<TM_NAMES_MAX>
  {
      /*** termination case: do nothing for TM_NAMES_MAX */
      static void init() { }
  };

  /**
   *  Initialize all of the TM algorithms
   */
  void tm_sys_init()
  {
      // manually register all behavior policies that we support.  We do
      // this via tail-recursive template metaprogramming
      MetaInitializer<0>::init();

      // guess a default configuration, then check env for a better option
      const char* cfg = \"NOrec\";
      const char* configstring = getenv(\"STM_CONFIG\");
      if (configstring)
          cfg = configstring;
      else
      {
          printf(\"STM_CONFIG environment variable not found... using %s\\n\", cfg);
      	  alg_index = stm::NOrec;
      }

      bool found = false;
      for (int i = 0; i < TM_NAMES_MAX; ++i) {
          const char* name = tm_info[i].tm_getalgname();
          if (0 == strcmp(cfg, name)) {

              rollback_ = tm_info[i].rollback;
              tm_begin_ = tm_info[i].tm_begin;
              tm_end_ = tm_info[i].tm_end;
              tm_getalgname_ = tm_info[i].tm_getalgname;
              tm_alloc_ = tm_info[i].tm_alloc;
              tm_free_ = tm_info[i].tm_free;
              tm_read_ = tm_info[i].tm_read;
              tm_write_ = tm_info[i].tm_write;

	      
              found = true;
	      alg_index = (TM_NAMES)i;
              break;
          }
      }
      printf(\"STM library configured using config == %s\\n\", cfg);
  }

  char* trueAlgName = NULL;
  const char* tm_getalgname()
  {
      if (trueAlgName)
          return trueAlgName;

      const char* s1 = \"AdapTM\";
      const char* s2 = tm_getalgname_();
      size_t l1 = strlen(s1);
      size_t l2 = strlen(s2);
      trueAlgName = (char*)malloc((l1+l2+3)*sizeof(char));
      strcpy(trueAlgName, s1);
      trueAlgName[l1] = trueAlgName[l1+1] = ':';
      strcpy(&trueAlgName[l1+2], s2);
      return trueAlgName;
  }

  /**
   *  We don't need, and don't want, to use the REGISTER_TM_FOR_XYZ macros,
   *  but we still need to make sure that there is an initTM<AdapTM> symbol:
   */
  template <> void initTM<AdapTM>() { }
}";

=======
#!/usr/bin/perl

# The purpose of this script is to automatically generate
# read/write/begin/commit methods when STM_INST_SWITCHADAPT is defined


# Start by printing a copyright and a warning
print "/**
 *  Copyright (C) 2011
 *    University of Rochester Department of Computer Science
 *      and
 *    Lehigh University Department of Computer Science and Engineering
 *
 *  License: Modified BSD
 *           Please see the file LICENSE.RSTM for licensing information
 */

/**
 *  Note: This file is automatically generated.  You should not modify
 *  it directly.
 */

";

print "
/**
 *  This file only has meaning when STM_INST_SWITCHADAPT is defined, but
 *  rather than put conditionals in the makefile, we guard the contents
 *  of the file.
 */
#ifdef STM_INST_SWITCHADAPT
";

# now print the includes that we will need
print "
#include \"../algs/algs.hpp\"
#include \"../Diagnostics.hpp\"
";

# put all declarations in namespace stm
print "
namespace stm
{

";

# It is expected that the command line contains the names of all algorithms,
# and nothing else.  Nonetheless, we'll copy the array to one with a better
# name
@algs = @ARGV;

# We'll discuss the generation of the tmbegin function in detail.  From
# there, the rest are easy.
#
# The basic structure of tmbegin is that it has a big switch statement, which
# calls exactly one of the many algorithms Begin functions, based on the
# value of curr_policy.ALG_ID.
#
# Unfortunately, there is no header file that declares the Begin function for
# each of the algorithms.  Thus we need to pre-declare these functions first.
# For algorithm X, the declaration of the begin function is void
# XBegin(TX_LONE_PARAMETER).

# Step 1: declare the Begin functions:
foreach my $alg (@algs) {
    print "  void ${alg}Begin(TX_LONE_PARAMETER);\n"
}

# Step 2: generate the tmbegin function prelude
print "
  void tmbegin(TX_LONE_PARAMETER)
  {
      switch (curr_policy.ALG_ID)
      {
";

# Step 3: generate the cases
foreach my $alg (@algs) {
print "        case ${alg}:
          ${alg}Begin(TX_LONE_ARG);
          break;\n"
}

# Step 4: generate the tmbegin function epilogue
print "        default:
          UNRECOVERABLE(\"Unrecognized Algorithm\");
      }
  }

";

# Now we can do tmcommit.  The only difference here is that the function
# declaration is slightly more complex

foreach my $alg (@algs) {
    print "  TM_FASTCALL void ${alg}Commit(TX_LONE_PARAMETER);\n"
}

print "
  void tmcommit(TX_LONE_PARAMETER)
  {
      switch (curr_policy.ALG_ID)
      {
";

foreach my $alg (@algs) {
print "        case ${alg}:
          ${alg}Commit(TX_LONE_ARG);
          break;\n"
}

print "        default:
          UNRECOVERABLE(\"Unrecognized Algorithm\");
      }
  }

";

# tmwrite takes parameters, but otherwise is the same as tmcommit

foreach my $alg (@algs) {
    print "  TM_FASTCALL void ${alg}Write(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,));\n"
}

print "
  void tmwrite(TX_FIRST_PARAMETER STM_WRITE_SIG(addr,val,))
  {
      switch (curr_policy.ALG_ID)
      {
";

foreach my $alg (@algs) {
print "        case ${alg}:
          ${alg}Write(TX_FIRST_ARG addr, val);
          break;\n"
}

print "        default:
          UNRECOVERABLE(\"Unrecognized Algorithm\");
      }
  }

";

# tmread takes parameters and also has a return value

foreach my $alg (@algs) {
    print "  TM_FASTCALL void* ${alg}Read(TX_FIRST_PARAMETER STM_READ_SIG(addr,));\n"
}

print "
  void* tmread(TX_FIRST_PARAMETER STM_READ_SIG(addr,))
  {
      switch (curr_policy.ALG_ID)
      {
";

foreach my $alg (@algs) {
print "        case ${alg}:
          return ${alg}Read(TX_FIRST_ARG addr);
          break;\n"
}

print "        default:
          UNRECOVERABLE(\"Unrecognized Algorithm\");
      }
      return NULL;
  }

";

# close out the namespace and the #define guard
print "
} // namespace stm

#endif // STM_INST_SWITCHADAPT
";
>>>>>>> .r551
