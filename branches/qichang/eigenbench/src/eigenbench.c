
/* =============================================================================
 *
 * Copyright (C) Stanford University, 2010.  All Rights Reserved.
 * 
 * Authors: Sungpack Hong and Jared Casper
 *
 * =============================================================================
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "eigenbench.h"


//--------------------------------------------------
// This file contains the core EigenBench routine
//--------------------------------------------------
static tWord* array1=NULL; // HOT array
static tWord* array2=NULL; // HOT array
static tWord* array3=NULL; // HOT array
#define ALIGN 64 
 
static void *aligned_malloc(int size) { 
    void *mem = malloc(size+ALIGN+sizeof(void*)); 
    void **ptr = (void**)((long)(mem+ALIGN+sizeof(void*)) & ~(ALIGN-1)); 
    ptr[-1] = mem; 
    return ptr; 
} 
 
static void aligned_free(void *ptr) { 
    free(((void**)ptr)[-1]); 
} 



void eigenbench_init_arrays(int N, int A1, int A2, int A3) 
{
  if (array1 != NULL) {eigenbench_free_arrays();}
  array1 = (tWord*)aligned_malloc(A1* sizeof(tWord) );
  array2 = (tWord*)aligned_malloc(A2* N * sizeof(tWord) );
  array3 = (tWord*)aligned_malloc(A3* N * sizeof(tWord) );
//

  //-----------------------------------
  // clear out page-faults
  //-----------------------------------
  int i;
  for(i=0;i<A1;i+=256)
      array1[i] = 0;
  for(i=0;i<A2*N;i+=256)
      array2[i] = 0;
  for(i=0;i<A3*N;i+=256)
      array3[i] = 0;
}

void eigenbench_free_arrays() 
{
  if (array1 == NULL) return;
  aligned_free(array1); array1 =  NULL;
  aligned_free(array2); array2 = NULL;
  aligned_free(array3); array3 = NULL;
}



typedef enum ActionEnum {
  NO_ACT, READ_HOT, WRITE_HOT, READ_MILD, WRITE_MILD
} Action;

static inline Action roll_action(unsigned* seed, int *r1, int *w1, int *r2, int *w2) 
{
  int total = *r1 + *w1 + *r2 + *w2;
  int value = uniform(seed, total, 1);
  
  Action a=NO_ACT;
  if (value<=*r1)                {*r1--; a = READ_HOT;}
  else if (value<=(*r1+*w1))     {*w1--; a = WRITE_HOT;}
  else if (value<=(*r1+*w1+*r2)) {*r2--; a = READ_MILD;}
  else if (value<=total)         {*w2--; a = WRITE_MILD;}
  return a;
}

#include <assert.h>

#define DICE_RNG    1024 
static inline int roll_addr(
    uint* seed, int range, 
    int lct, int* hist, int* idx)
{ 
    if (lct == 0) return uniform(seed, range, 0);

    if (((*idx)==0) || (uniform(seed, DICE_RNG,0) > lct))
    {
        //if (*idx > 0) assert(0);
        int t = (*idx) % EB_HISTORY_SZ;
        int x = hist[t] = uniform(seed, range, 0);
        (*idx)++;
        return x;
    }
    else {
        int k = uniform(seed, (*idx < EB_HISTORY_SZ) ? *idx : EB_HISTORY_SZ, 0);
        //assert(0);
        return hist[k];
    }
}

static inline void local_ops(
        uint* seed, int R, int W, 
        int A3, int tid, int nops, tWord *val) 
{
  int r = R;
  int w = W;
  int loop = r + w;
  int z = 0;
  int i;
  for (i = 0; i < loop; i++) {
    int dummy;
    int dcnt = 0;
    int index = roll_addr(seed, A3, 0, &dummy, &dcnt) + A3 * tid;
    Action a = roll_action(seed, &r, &w, &z, &z);
    switch(a) {
    case READ_HOT: *val += array3[index]; break;
    case WRITE_HOT: array3[index] += *val; break;
    }
  }
  for(i=0; i<nops; i++) nop();
}
tWord dummy = 0;

//------------------------------------------------
// main benchmark core
// : each thread should be called with unique tid and seed
//-------------------------------------------------
tWord eigenbench_core(TM_ARGDECL int tid, unsigned* seed, int* opts) 
{

  int r1,r2,w1,w2;
  tWord val=0;
  tWord val2=0;

  int _R1 = opts[R1];
  int _R2 = opts[R2];
  int _W1 = opts[W1];
  int _W2 = opts[W2];
  int _R3i = opts[R3i];
  int _W3i = opts[W3i];
  int _R3o = opts[R3o];
  int _W3o = opts[W3o];
  int _A1 = opts[A1];
  int _A2 = opts[A2];
  int _A3 = opts[A3];
  int _NOPi = opts[NOPi];
  int _NOPo = opts[NOPo];
  int total = _W1 + _W2 + _R1 + _R2;
  int in = (_R3i + _W3i + _NOPi) > 0; // Boolean
  int out = (_R3o + _W3o + _NOPo) > 0;


  int LOOP = opts[loops];
  int k_in = opts[Ki];
  int k_out = opts[Ko];
  int lct = opts[LCT];
  int persist = opts[PERSIST];

  int hist1[EB_HISTORY_SZ]; // history for A1
  int hist2[EB_HISTORY_SZ]; // historu for A2

  //struct timeval start, stop;
  int i, j;
  int i2 =0;

  int hidx1,hidx2;
  for(i = 0; i < LOOP;  i++) {
    r1 = _R1; r2 = _R2; w1 = _W1; w2 = _W2;
    int index;  
    int j2=0;
    unsigned seed_saved = *seed;
    val2=0;

    TM_BEGIN();
    hidx1=hidx2=0;  // history index
    if (persist) *seed = seed_saved;

    // work inside TX
    for(j = 0; j < total ; j ++) {
      switch(roll_action (seed, &r1, &w1, &r2, &w2)) {
      case READ_HOT:
        index = roll_addr(seed, _A1, lct, hist1, &hidx1);
        val += STM_READ(array1[index])+1;
        break;
      case WRITE_HOT:
        index = roll_addr(seed, _A1, lct, hist1, &hidx1);
        STM_WRITE(array1[index], val);
        break;
      case READ_MILD:
        index = roll_addr(seed, _A2, lct, hist2, &hidx2);
        index += _A2*tid;
        val += STM_READ(array2[index])+1;
        break;
      case WRITE_MILD:
        index = roll_addr(seed, _A2, lct, hist2, &hidx2);
        index += _A2*tid;
        STM_WRITE(array2[index], val);
        break; 
      }
      if (in && (++j2 == k_in)) {
        j2 = 0;
        local_ops(seed, _R3i, _W3i, _A3, tid, _NOPi, &val2); 
      }
    }
    TM_END();

    val += val2;
    
    // work outside TM
    if (out && (++i2  == k_out)) {
        i2=0;
        local_ops(seed, _R3o, _W3o, _A3, tid, _NOPo, &val);
    }
  } // end of LOOPS

  return val; // to prevent compiler optimization
}

