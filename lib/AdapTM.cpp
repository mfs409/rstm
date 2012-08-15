/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#include <stdint.h>
#include <iostream>
#include <cstdlib>
#include <setjmp.h> // factor this out into the API?
#include "tx.hpp"
#include "platform.hpp"
#include "locks.hpp"
#include "metadata.hpp"
#include "adaptivity.hpp"
#include "alg.hpp"

/**
 * This STM is implemented differently than all others.  We don't give it a
 * custom namespace, and we don't rely on tx.cpp.  Instead, we implement
 * everything directly within this file.  In that way, we can get all of our
 * adaptivity hooks to work correctly.
 *
 * NB: right now, we pick an algorithm at begin time, but we don't actually
 *     adapt yet
 */

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
          std::cout << "Thread: "       << threads[i]->id
                    << "; RO Commits: " << threads[i]->commits_ro
                    << "; RW Commits: " << threads[i]->commits_rw
                    << "; Aborts: "     << threads[i]->aborts
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


  void tm_begin(void * buf)
  {
     printf("alg_index = %d", alg_index);
     if(alg_index >= 0)
     {
         printf("running tm_begin of alg_index %d", alg_index);
	 switch((TM_NAMES)alg_index)
	 {
		case NOrec:
			printf("norec's tm_begin");
			norec::tm_begin(buf);
			break;
		case TML:
			printf("tml's tm_begin");
			tml::tm_begin(buf);
			break;
		case CohortsEager:
			printf("cohortseager's tm_begin");
			cohortseager::tm_begin(buf);
			break;
		case Cohorts:
			printf("Cohorts's tm_begin");
			cohorts::tm_begin(buf);
			break;
		case CTokenTurbo:
			printf("ctokenturbo's tm_begin");
			ctokenturbo::tm_begin(buf);
			break;
		case CToken:
			printf("ctoken's tm_begin");
			ctoken::tm_begin(buf);
			break;
/*		case LLT:
			printf("llt's tm_begin");
			llt::tm_begin(buf);
			break;
		case OrecEagerRedo:
			printf("oreceagerredo's tm_begin");
			oreceagerredo::tm_begin(buf);
			break;
		case OrecELA:
			printf("orecela's tm_begin");
			orecela::tm_begin(buf);
			break;
		case OrecALA:
			printf("orecala's tm_begin");
			oreclazy::tm_begin(buf);
			break;
		case OrecLazy:
			printf("oreclazy's tm_begin");
			oreclazy::tm_begin(buf);
			break;
		case OrecEager:
			printf("oreceager's tm_begin");
			oreceager::tm_begin(buf);
			break;
		case NOrecHB:
			printf("norechb's tm_begin");
			norechb::tm_begin(buf);
			break;
		case OrecLazyBackoff:
			printf("oreclazybackoff's tm_begin");
			oreclazybackoff::tm_begin(buf);
			break;
		case OrecLazyHB:
			printf("oreclazyhb's tm_begin");
			oreclazyhb::tm_begin(buf);
			break;
		case OrecLazyHour:
			printf("oreclazyhour's tm_begin");
			oreclazyhour::tm_begin(buf);
			break;
		case NOrecBackoff:
			printf("norecbackoff's tm_begin");
			norecbackoff::tm_begin(buf);
			break;
		case NOrecHour:
			printf("norechour's tm_begin\n");
			norechour::tm_begin(buf);
			break;
		case OrecEagerHour:
			printf("oreceagerhour's tm_begin\n");
			oreceagerhour::tm_begin(buf);
			break;
		case OrecEagerHB:
			printf("oreceagerhb's tm_begin\n");
			oreceagerhb::tm_begin(buf);
			break;
		case OrecEagerBackoff:
			printf("oreceagerbackoff's tm_begin\n");
			oreceagerbackoff::tm_begin(buf);
			break;
*/
		case CGL:
			printf("cgl's tm_begin\n");
			cgl::tm_begin(buf);
			break;
		case AdapTM:
			printf("adaptm's tm_begin\n");
			stm::tm_begin(buf);
			break;
		default:
			assert(0);
			break;
         };
     }else
     { assert(0); tm_begin_(buf); };
  }

  void tm_end()
  {
	printf("alg_index = %d", alg_index);
    	if(alg_index >= 0)
  	{
       		printf("running tm_end of alg_index %d", alg_index);
	 	switch((TM_NAMES)alg_index)
	 	{
		case NOrec:
			printf("norec's tm_end");
			norec::tm_end();
			break;
		case TML:
			printf("tml's tm_end");
			tml::tm_end();
			break;
		case CohortsEager:
			printf("cohortseager's tm_end");
			cohortseager::tm_end();
			break;
		case Cohorts:
			printf("Cohorts's tm_end");
			cohorts::tm_end();
			break;
		case CTokenTurbo:
			printf("ctokenturbo's tm_end");
			ctokenturbo::tm_end();
			break;
		case CToken:
			printf("ctoken's tm_end");
			ctoken::tm_end();
			break;
/*		case LLT:
			printf("llt's tm_begin");
			llt::tm_begin(buf);
			break;
		case OrecEagerRedo:
			printf("oreceagerredo's tm_begin");
			oreceagerredo::tm_begin(buf);
			break;
		case OrecELA:
			printf("orecela's tm_begin");
			orecela::tm_begin(buf);
			break;
		case OrecALA:
			printf("orecala's tm_begin");
			oreclazy::tm_begin(buf);
			break;
		case OrecLazy:
			printf("oreclazy's tm_begin");
			oreclazy::tm_begin(buf);
			break;
		case OrecEager:
			printf("oreceager's tm_begin");
			oreceager::tm_begin(buf);
			break;
		case NOrecHB:
			printf("norechb's tm_begin");
			norechb::tm_begin(buf);
			break;
		case OrecLazyBackoff:
			printf("oreclazybackoff's tm_begin");
			oreclazybackoff::tm_begin(buf);
			break;
		case OrecLazyHB:
			printf("oreclazyhb's tm_begin");
			oreclazyhb::tm_begin(buf);
			break;
		case OrecLazyHour:
			printf("oreclazyhour's tm_begin");
			oreclazyhour::tm_begin(buf);
			break;
		case NOrecBackoff:
			printf("norecbackoff's tm_begin");
			norecbackoff::tm_begin(buf);
			break;
		case NOrecHour:
			printf("norechour's tm_begin\n");
			norechour::tm_begin(buf);
			break;
		case OrecEagerHour:
			printf("oreceagerhour's tm_begin\n");
			oreceagerhour::tm_begin(buf);
			break;
		case OrecEagerHB:
			printf("oreceagerhb's tm_begin\n");
			oreceagerhb::tm_begin(buf);
			break;
		case OrecEagerBackoff:
			printf("oreceagerbackoff's tm_begin\n");
			oreceagerbackoff::tm_begin(buf);
			break;
*/
		case CGL:
			printf("cgl's tm_end\n");
			cgl::tm_end();
			break;
		case AdapTM:
			printf("adaptm's tm_end\n");
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
	printf("alg_index = %d", alg_index);
	if(alg_index >= 0)
    	{
        	printf("running tm_alloc of alg_index %d", alg_index);
	 	switch((TM_NAMES)alg_index)
	 	{
		case NOrec:
			printf("norec's tm_alloc");
			norec::tm_alloc(s);
			break;
		case TML:
			printf("tml's tm_alloc");
			tml::tm_alloc(s);
			break;
		case CohortsEager:
			printf("cohortseager's tm_alloc");
			cohortseager::tm_alloc(s);
			break;
		case Cohorts:
			printf("Cohorts's tm_alloc");
			cohorts::tm_alloc(s);
			break;
		case CTokenTurbo:
			printf("ctokenturbo's tm_alloc");
			ctokenturbo::tm_alloc(s);
			break;
		case CToken:
			printf("ctoken's tm_alloc");
			ctoken::tm_alloc(s);
			break;
/*		case LLT:
			printf("llt's tm_begin");
			llt::tm_begin(buf);
			break;
		case OrecEagerRedo:
			printf("oreceagerredo's tm_begin");
			oreceagerredo::tm_begin(buf);
			break;
		case OrecELA:
			printf("orecela's tm_begin");
			orecela::tm_begin(buf);
			break;
		case OrecALA:
			printf("orecala's tm_begin");
			oreclazy::tm_begin(buf);
			break;
		case OrecLazy:
			printf("oreclazy's tm_begin");
			oreclazy::tm_begin(buf);
			break;
		case OrecEager:
			printf("oreceager's tm_begin");
			oreceager::tm_begin(buf);
			break;
		case NOrecHB:
			printf("norechb's tm_begin");
			norechb::tm_begin(buf);
			break;
		case OrecLazyBackoff:
			printf("oreclazybackoff's tm_begin");
			oreclazybackoff::tm_begin(buf);
			break;
		case OrecLazyHB:
			printf("oreclazyhb's tm_begin");
			oreclazyhb::tm_begin(buf);
			break;
		case OrecLazyHour:
			printf("oreclazyhour's tm_begin");
			oreclazyhour::tm_begin(buf);
			break;
		case NOrecBackoff:
			printf("norecbackoff's tm_begin");
			norecbackoff::tm_begin(buf);
			break;
		case NOrecHour:
			printf("norechour's tm_begin\n");
			norechour::tm_begin(buf);
			break;
		case OrecEagerHour:
			printf("oreceagerhour's tm_begin\n");
			oreceagerhour::tm_begin(buf);
			break;
		case OrecEagerHB:
			printf("oreceagerhb's tm_begin\n");
			oreceagerhb::tm_begin(buf);
			break;
		case OrecEagerBackoff:
			printf("oreceagerbackoff's tm_begin\n");
			oreceagerbackoff::tm_begin(buf);
			break;
*/
		case CGL:
			printf("cgl's tm_alloc\n");
			cgl::tm_alloc(s);
			break;
/		case AdapTM:
			printf("adaptm's tm_alloc\n");
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
	printf("alg_index = %d", alg_index);
    	if(alg_index >= 0)
    	{
      		printf("running tm_free of alg_index %d", alg_index);
		switch((TM_NAMES)alg_index)
	 	{
		case NOrec:
			printf("norec's tm_free");
			norec::tm_free(p);
			break;
		case TML:
			printf("tml's tm_free");
			tml::tm_free(p);
			break;
		case CohortsEager:
			printf("cohortseager's tm_free");
			cohortseager::tm_free(p);
			break;
		case Cohorts:
			printf("Cohorts's tm_free");
			cohorts::tm_free(p);
			break;
		case CTokenTurbo:
			printf("ctokenturbo's tm_free");
			ctokenturbo::tm_free(p);
			break;
		case CToken:
			printf("ctoken's tm_free");
			ctoken::tm_free(p);
			break;
/*		case LLT:
			printf("llt's tm_begin");
			llt::tm_begin(buf);
			break;
		case OrecEagerRedo:
			printf("oreceagerredo's tm_begin");
			oreceagerredo::tm_begin(buf);
			break;
		case OrecELA:
			printf("orecela's tm_begin");
			orecela::tm_begin(buf);
			break;
		case OrecALA:
			printf("orecala's tm_begin");
			oreclazy::tm_begin(buf);
			break;
		case OrecLazy:
			printf("oreclazy's tm_begin");
			oreclazy::tm_begin(buf);
			break;
		case OrecEager:
			printf("oreceager's tm_begin");
			oreceager::tm_begin(buf);
			break;
		case NOrecHB:
			printf("norechb's tm_begin");
			norechb::tm_begin(buf);
			break;
		case OrecLazyBackoff:
			printf("oreclazybackoff's tm_begin");
			oreclazybackoff::tm_begin(buf);
			break;
		case OrecLazyHB:
			printf("oreclazyhb's tm_begin");
			oreclazyhb::tm_begin(buf);
			break;
		case OrecLazyHour:
			printf("oreclazyhour's tm_begin");
			oreclazyhour::tm_begin(buf);
			break;
		case NOrecBackoff:
			printf("norecbackoff's tm_begin");
			norecbackoff::tm_begin(buf);
			break;
		case NOrecHour:
			printf("norechour's tm_begin\n");
			norechour::tm_begin(buf);
			break;
		case OrecEagerHour:
			printf("oreceagerhour's tm_begin\n");
			oreceagerhour::tm_begin(buf);
			break;
		case OrecEagerHB:
			printf("oreceagerhb's tm_begin\n");
			oreceagerhb::tm_begin(buf);
			break;
		case OrecEagerBackoff:
			printf("oreceagerbackoff's tm_begin\n");
			oreceagerbackoff::tm_begin(buf);
			break;
*/
		case CGL:
			printf("cgl's tm_free\n");
			cgl::tm_free(p);
			break;
		case AdapTM:
			printf("adaptm's tm_free\n");
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
	printf("alg_index = %d", alg_index);
	if(alg_index >= 0)
	{
		printf("running tm_read of alg_index %d", alg_index);
		switch((TM_NAMES)alg_index)
	 	{
		case NOrec:
			printf("norec's tm_read");
			norec::tm_read(addr);
			break;
		case TML:
			printf("tml's tm_read");
			tml::tm_read(addr);
			break;
		case CohortsEager:
			printf("cohortseager's tm_read");
			cohortseager::tm_read(addr);
			break;
		case Cohorts:
			printf("Cohorts's tm_read");
			cohorts::tm_read(addr);
			break;
		case CTokenTurbo:
			printf("ctokenturbo's tm_read");
			ctokenturbo::tm_read(addr);
			break;
		case CToken:
			printf("ctoken's tm_read");
			ctoken::tm_read(addr);
			break;
/*		case LLT:
			printf("llt's tm_begin");
			llt::tm_begin(buf);
			break;
		case OrecEagerRedo:
			printf("oreceagerredo's tm_begin");
			oreceagerredo::tm_begin(buf);
			break;
		case OrecELA:
			printf("orecela's tm_begin");
			orecela::tm_begin(buf);
			break;
		case OrecALA:
			printf("orecala's tm_begin");
			oreclazy::tm_begin(buf);
			break;
		case OrecLazy:
			printf("oreclazy's tm_begin");
			oreclazy::tm_begin(buf);
			break;
		case OrecEager:
			printf("oreceager's tm_begin");
			oreceager::tm_begin(buf);
			break;
		case NOrecHB:
			printf("norechb's tm_begin");
			norechb::tm_begin(buf);
			break;
		case OrecLazyBackoff:
			printf("oreclazybackoff's tm_begin");
			oreclazybackoff::tm_begin(buf);
			break;
		case OrecLazyHB:
			printf("oreclazyhb's tm_begin");
			oreclazyhb::tm_begin(buf);
			break;
		case OrecLazyHour:
			printf("oreclazyhour's tm_begin");
			oreclazyhour::tm_begin(buf);
			break;
		case NOrecBackoff:
			printf("norecbackoff's tm_begin");
			norecbackoff::tm_begin(buf);
			break;
		case NOrecHour:
			printf("norechour's tm_begin\n");
			norechour::tm_begin(buf);
			break;
		case OrecEagerHour:
			printf("oreceagerhour's tm_begin\n");
			oreceagerhour::tm_begin(buf);
			break;
		case OrecEagerHB:
			printf("oreceagerhb's tm_begin\n");
			oreceagerhb::tm_begin(buf);
			break;
		case OrecEagerBackoff:
			printf("oreceagerbackoff's tm_begin\n");
			oreceagerbackoff::tm_begin(buf);
			break;
*/
		case CGL:
			printf("cgl's tm_read\n");
			cgl::tm_read(addr);
			break;
		case AdapTM:
			printf("adaptm's tm_read\n");
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
	printf("alg_index = %d", alg_index);
 	if(alg_index >= 0)
	{
		printf("running tm_write of alg_index %d", alg_index);
		switch((TM_NAMES)alg_index)
	 	{
		case NOrec:
			printf("norec's tm_write");
			norec::tm_write(addr, val);
			break;
		case TML:
			printf("tml's tm_write");
			tml::tm_write(addr, val);
			break;
		case CohortsEager:
			printf("cohortseager's tm_write");
			cohortseager::tm_write(addr, val);
			break;
		case Cohorts:
			printf("Cohorts's tm_write");
			cohorts::tm_write(addr, val);
			break;
		case CTokenTurbo:
			printf("ctokenturbo's tm_write");
			ctokenturbo::tm_write(addr, val);
			break;
		case CToken:
			printf("ctoken's tm_write");
			ctoken::tm_write(addr, val);
			break;
/*		case LLT:
			printf("llt's tm_begin");
			llt::tm_begin(buf);
			break;
		case OrecEagerRedo:
			printf("oreceagerredo's tm_begin");
			oreceagerredo::tm_begin(buf);
			break;
		case OrecELA:
			printf("orecela's tm_begin");
			orecela::tm_begin(buf);
			break;
		case OrecALA:
			printf("orecala's tm_begin");
			oreclazy::tm_begin(buf);
			break;
		case OrecLazy:
			printf("oreclazy's tm_begin");
			oreclazy::tm_begin(buf);
			break;
		case OrecEager:
			printf("oreceager's tm_begin");
			oreceager::tm_begin(buf);
			break;
		case NOrecHB:
			printf("norechb's tm_begin");
			norechb::tm_begin(buf);
			break;
		case OrecLazyBackoff:
			printf("oreclazybackoff's tm_begin");
			oreclazybackoff::tm_begin(buf);
			break;
		case OrecLazyHB:
			printf("oreclazyhb's tm_begin");
			oreclazyhb::tm_begin(buf);
			break;
		case OrecLazyHour:
			printf("oreclazyhour's tm_begin");
			oreclazyhour::tm_begin(buf);
			break;
		case NOrecBackoff:
			printf("norecbackoff's tm_begin");
			norecbackoff::tm_begin(buf);
			break;
		case NOrecHour:
			printf("norechour's tm_begin\n");
			norechour::tm_begin(buf);
			break;
		case OrecEagerHour:
			printf("oreceagerhour's tm_begin\n");
			oreceagerhour::tm_begin(buf);
			break;
		case OrecEagerHB:
			printf("oreceagerhb's tm_begin\n");
			oreceagerhb::tm_begin(buf);
			break;
		case OrecEagerBackoff:
			printf("oreceagerbackoff's tm_begin\n");
			oreceagerbackoff::tm_begin(buf);
			break;
*/
		case CGL:
			printf("cgl's tm_write\n");
			cgl::tm_write(addr, val);
			break;
		case AdapTM:
			printf("adaptm's tm_write\n");
			stm::tm_write(addr, val);
			break;
		default:
			assert(0);
			break;
         	};
	}else
	{ assert(0); tm_write_(addr, val); }
  } 

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
      const char* cfg = "NOrec";
      const char* configstring = getenv("STM_CONFIG");
      if (configstring)
          cfg = configstring;
      else
      {
          printf("STM_CONFIG environment variable not found... using %s\n", cfg);
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
	      alg_index = i;
              break;
          }
      }
      printf("STM library configured using config == %s\n", cfg);
  }

  char* trueAlgName = NULL;
  const char* tm_getalgname()
  {
      if (trueAlgName)
          return trueAlgName;

      const char* s1 = "AdapTM";
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
}
