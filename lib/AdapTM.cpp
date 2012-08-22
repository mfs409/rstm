
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
		norec::tm_begin(buf);
		break;
	case TML:
		tml::tm_begin(buf);
		break;
	case CohortsEager:
		cohortseager::tm_begin(buf);
		break;
	case Cohorts:
		cohorts::tm_begin(buf);
		break;
	case CTokenTurbo:
		ctokenturbo::tm_begin(buf);
		break;
	case CToken:
		ctoken::tm_begin(buf);
		break;
	case LLT:
		llt::tm_begin(buf);
		break;
	case OrecEagerRedo:
		oreceagerredo::tm_begin(buf);
		break;
	case OrecELA:
		orecela::tm_begin(buf);
		break;
	case OrecALA:
		orecala::tm_begin(buf);
		break;
	case OrecLazy:
		oreclazy::tm_begin(buf);
		break;
	case OrecEager:
		oreceager::tm_begin(buf);
		break;
	case NOrecHB:
		norechb::tm_begin(buf);
		break;
	case OrecLazyBackoff:
		oreclazybackoff::tm_begin(buf);
		break;
	case OrecLazyHB:
		oreclazyhb::tm_begin(buf);
		break;
	case OrecLazyHour:
		oreclazyhour::tm_begin(buf);
		break;
	case NOrecBackoff:
		norecbackoff::tm_begin(buf);
		break;
	case NOrecHour:
		norechour::tm_begin(buf);
		break;
	case OrecEagerHour:
		oreceagerhour::tm_begin(buf);
		break;
	case OrecEagerHB:
		oreceagerhb::tm_begin(buf);
		break;
	case OrecEagerBackoff:
		oreceagerbackoff::tm_begin(buf);
		break;
	case CGL:
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
		norec::tm_end(buf);
		break;
	case TML:
		tml::tm_end(buf);
		break;
	case CohortsEager:
		cohortseager::tm_end(buf);
		break;
	case Cohorts:
		cohorts::tm_end(buf);
		break;
	case CTokenTurbo:
		ctokenturbo::tm_end(buf);
		break;
	case CToken:
		ctoken::tm_end(buf);
		break;
	case LLT:
		llt::tm_end(buf);
		break;
	case OrecEagerRedo:
		oreceagerredo::tm_end(buf);
		break;
	case OrecELA:
		orecela::tm_end(buf);
		break;
	case OrecALA:
		orecala::tm_end(buf);
		break;
	case OrecLazy:
		oreclazy::tm_end(buf);
		break;
	case OrecEager:
		oreceager::tm_end(buf);
		break;
	case NOrecHB:
		norechb::tm_end(buf);
		break;
	case OrecLazyBackoff:
		oreclazybackoff::tm_end(buf);
		break;
	case OrecLazyHB:
		oreclazyhb::tm_end(buf);
		break;
	case OrecLazyHour:
		oreclazyhour::tm_end(buf);
		break;
	case NOrecBackoff:
		norecbackoff::tm_end(buf);
		break;
	case NOrecHour:
		norechour::tm_end(buf);
		break;
	case OrecEagerHour:
		oreceagerhour::tm_end(buf);
		break;
	case OrecEagerHB:
		oreceagerhb::tm_end(buf);
		break;
	case OrecEagerBackoff:
		oreceagerbackoff::tm_end(buf);
		break;
	case CGL:
		cgl::tm_end(buf);
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
		norec::tm_alloc(s);
		break;
	case TML:
		tml::tm_alloc(s);
		break;
	case CohortsEager:
		cohortseager::tm_alloc(s);
		break;
	case Cohorts:
		cohorts::tm_alloc(s);
		break;
	case CTokenTurbo:
		ctokenturbo::tm_alloc(s);
		break;
	case CToken:
		ctoken::tm_alloc(s);
		break;
	case LLT:
		llt::tm_alloc(s);
		break;
	case OrecEagerRedo:
		oreceagerredo::tm_alloc(s);
		break;
	case OrecELA:
		orecela::tm_alloc(s);
		break;
	case OrecALA:
		orecala::tm_alloc(s);
		break;
	case OrecLazy:
		oreclazy::tm_alloc(s);
		break;
	case OrecEager:
		oreceager::tm_alloc(s);
		break;
	case NOrecHB:
		norechb::tm_alloc(s);
		break;
	case OrecLazyBackoff:
		oreclazybackoff::tm_alloc(s);
		break;
	case OrecLazyHB:
		oreclazyhb::tm_alloc(s);
		break;
	case OrecLazyHour:
		oreclazyhour::tm_alloc(s);
		break;
	case NOrecBackoff:
		norecbackoff::tm_alloc(s);
		break;
	case NOrecHour:
		norechour::tm_alloc(s);
		break;
	case OrecEagerHour:
		oreceagerhour::tm_alloc(s);
		break;
	case OrecEagerHB:
		oreceagerhb::tm_alloc(s);
		break;
	case OrecEagerBackoff:
		oreceagerbackoff::tm_alloc(s);
		break;
	case CGL:
		cgl::tm_alloc(s);
		break;

		case AdapTM:
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
		norec::tm_free(p);
		break;
	case TML:
		tml::tm_free(p);
		break;
	case CohortsEager:
		cohortseager::tm_free(p);
		break;
	case Cohorts:
		cohorts::tm_free(p);
		break;
	case CTokenTurbo:
		ctokenturbo::tm_free(p);
		break;
	case CToken:
		ctoken::tm_free(p);
		break;
	case LLT:
		llt::tm_free(p);
		break;
	case OrecEagerRedo:
		oreceagerredo::tm_free(p);
		break;
	case OrecELA:
		orecela::tm_free(p);
		break;
	case OrecALA:
		orecala::tm_free(p);
		break;
	case OrecLazy:
		oreclazy::tm_free(p);
		break;
	case OrecEager:
		oreceager::tm_free(p);
		break;
	case NOrecHB:
		norechb::tm_free(p);
		break;
	case OrecLazyBackoff:
		oreclazybackoff::tm_free(p);
		break;
	case OrecLazyHB:
		oreclazyhb::tm_free(p);
		break;
	case OrecLazyHour:
		oreclazyhour::tm_free(p);
		break;
	case NOrecBackoff:
		norecbackoff::tm_free(p);
		break;
	case NOrecHour:
		norechour::tm_free(p);
		break;
	case OrecEagerHour:
		oreceagerhour::tm_free(p);
		break;
	case OrecEagerHB:
		oreceagerhb::tm_free(p);
		break;
	case OrecEagerBackoff:
		oreceagerbackoff::tm_free(p);
		break;
	case CGL:
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
		norec::tm_read(addr);
		break;
	case TML:
		tml::tm_read(addr);
		break;
	case CohortsEager:
		cohortseager::tm_read(addr);
		break;
	case Cohorts:
		cohorts::tm_read(addr);
		break;
	case CTokenTurbo:
		ctokenturbo::tm_read(addr);
		break;
	case CToken:
		ctoken::tm_read(addr);
		break;
	case LLT:
		llt::tm_read(addr);
		break;
	case OrecEagerRedo:
		oreceagerredo::tm_read(addr);
		break;
	case OrecELA:
		orecela::tm_read(addr);
		break;
	case OrecALA:
		orecala::tm_read(addr);
		break;
	case OrecLazy:
		oreclazy::tm_read(addr);
		break;
	case OrecEager:
		oreceager::tm_read(addr);
		break;
	case NOrecHB:
		norechb::tm_read(addr);
		break;
	case OrecLazyBackoff:
		oreclazybackoff::tm_read(addr);
		break;
	case OrecLazyHB:
		oreclazyhb::tm_read(addr);
		break;
	case OrecLazyHour:
		oreclazyhour::tm_read(addr);
		break;
	case NOrecBackoff:
		norecbackoff::tm_read(addr);
		break;
	case NOrecHour:
		norechour::tm_read(addr);
		break;
	case OrecEagerHour:
		oreceagerhour::tm_read(addr);
		break;
	case OrecEagerHB:
		oreceagerhb::tm_read(addr);
		break;
	case OrecEagerBackoff:
		oreceagerbackoff::tm_read(addr);
		break;
	case CGL:
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
		norec::tm_write(addr, val);
		break;
	case TML:
		tml::tm_write(addr, val);
		break;
	case CohortsEager:
		cohortseager::tm_write(addr, val);
		break;
	case Cohorts:
		cohorts::tm_write(addr, val);
		break;
	case CTokenTurbo:
		ctokenturbo::tm_write(addr, val);
		break;
	case CToken:
		ctoken::tm_write(addr, val);
		break;
	case LLT:
		llt::tm_write(addr, val);
		break;
	case OrecEagerRedo:
		oreceagerredo::tm_write(addr, val);
		break;
	case OrecELA:
		orecela::tm_write(addr, val);
		break;
	case OrecALA:
		orecala::tm_write(addr, val);
		break;
	case OrecLazy:
		oreclazy::tm_write(addr, val);
		break;
	case OrecEager:
		oreceager::tm_write(addr, val);
		break;
	case NOrecHB:
		norechb::tm_write(addr, val);
		break;
	case OrecLazyBackoff:
		oreclazybackoff::tm_write(addr, val);
		break;
	case OrecLazyHB:
		oreclazyhb::tm_write(addr, val);
		break;
	case OrecLazyHour:
		oreclazyhour::tm_write(addr, val);
		break;
	case NOrecBackoff:
		norecbackoff::tm_write(addr, val);
		break;
	case NOrecHour:
		norechour::tm_write(addr, val);
		break;
	case OrecEagerHour:
		oreceagerhour::tm_write(addr, val);
		break;
	case OrecEagerHB:
		oreceagerhb::tm_write(addr, val);
		break;
	case OrecEagerBackoff:
		oreceagerbackoff::tm_write(addr, val);
		break;
	case CGL:
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
	      alg_index = (TM_NAMES)i;
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