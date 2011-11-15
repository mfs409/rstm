#ifndef EIGENBENCH_H
#define EIGENBENCH_H

//---------------------------------------------
// Hooks for Thread,STM Library
// We used the same API as STAMP Benchmark 
//----------------------------------------------
#include <stdint.h>
#include <stdlib.h>
#include "thread.h" // Copied from STAMP Library. 

// exsample hooks implemented for TL2, SwissTM, and Unprotected Executon
#ifdef TL2
#include "stamp_api_tl2.h"      
#elif defined(SWISSTM)
#include "stamp_api_swisstm.h"
#elif defined(UNPROTECTED)
#include "stamp_api_unprotected.h"
#elif defined(RSTM)
#include "stamp_api_rstm.h"
#else 
# error "No STM defined"
#endif

//---------------------------------------------
// Below is the parameters of EigenBench
// N : number of threads
//
// Detailed explanation is available in the paper.
//---------------------------------------------

//--------------------------------------------
// Parameters (setup by main function)
//--------------------------------------------
enum optenum {
  NN,    loops,   A1,   A2,   A3,   R1,   W1,   R2,   W2,   R3i,   W3i,   R3o,   W3o,   NOPi,   NOPo,  Ki, Ko, LCT, PERSIST, M, NUMOPTS
};

#ifdef USE_STRICT_4B_WORD
typedef uint    tWord;
#else
typedef long        tWord;
#endif

//--------------------------------------------
// Main Benchmark function
//--------------------------------------------
void eigenbench_init_arrays(int N, int32_t A1, int32_t A2, int32_t A3);
tWord eigenbench_core(TM_ARGDECL int tid, unsigned* seed, int* opts);
void eigenbench_free_arrays(); 


#define EB_HISTORY_SZ   128
static inline int uniform(unsigned* seed, int max, int min)
{
  int32_t v = rand_r(seed);

  return ((double)v * ((double)(max - min) / RAND_MAX)) + min;
  
  // note:
  // If your system is bad at computing floating point numbers, use below technique instead. 
  // The result distribution can be skewed if (max-min) is not 2^n, however.
  //return (v % (max-min)) + min; 
}


#endif
