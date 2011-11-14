
/* =============================================================================
 *
 * Copyright (C) Stanford University, 2010.  All Rights Reserved.
 *
 * Authors: Sungpack Hong and Jared Casper
 *
 * =============================================================================
 */

//---------------------------------------------------
// This file contains the main() function that
// parses input file, setup parameters and calls the core
//---------------------------------------------------


// Standard C Library
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <getopt.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>

#include "eigenbench.h"

const char* opt_names[] = { // case does not matter, however
  "N", "loops", "A1", "A2", "A3", "R1", "W1", "R2", "W2", "R3i", "W3i", "R3o", "W3o", "NOPi", "NOPo", "Ki", "Ko", "LCT", "PERSIST", "M"
};
const int opt_default[] =  {
    8,  1000000,  65536, 1048576, 8192, // N, loop, A1, A2, A3
    10, 10, 20, 20,  // R1, W1, R2, W2, 
    0, 0, 0, 0,      //  R3i, W3i, R3o, W3o,
    0, 0, 0, 0,      // Nopi, Nopo, Ki, Ko, 
    0, 0, 1          // LCT, PERSIST, M
};

int global_seed;
int num_parameter_sets = 0;
int num_threads = 1;

// private options per thread
int** opts;

int parse_paramname(char* pname)
{
    int i;
    for(i=0;i<NUMOPTS;i++)
    {
        if (!strcasecmp(opt_names[i],pname)) {
            return i;
        }
    }
    return NUMOPTS;
}

void parse_paramfile(char* fname)
{
    char linebuf[2048];
    FILE* f = fopen(fname,"r");
    if (f==NULL) {
        printf("Cannot open parameter file: %s\n", fname);
    }
    int default_opts[NUMOPTS];
    int i;
    for(i=0;i<NUMOPTS;i++)
        default_opts[i] = opt_default[i];

    int N=1;
    while(fgets(linebuf, 2048,f) != NULL)
    {
        char* l = linebuf;
        if ((l[0] == ' ') || (l[0] == '\t') || (l[0]=='\r')) 
             l=  strtok(linebuf, " \t\n\r");
        if (l==NULL) continue;
        if (l[0] == '#') continue;
        if (l[0] == '*') continue; // specialcas

        char pname[512];
        int a1; 
        sscanf(l, "%s%d", pname, &a1);
        int p = parse_paramname(pname); 
        if (p == NUMOPTS) {
            printf("Warning: Ignoring unkown paramter:%s\n", pname);
            continue;
        }
        if (p==M) {
            num_parameter_sets = a1;
            continue;
        }
        default_opts[p] = a1;
    }
    num_threads = N = default_opts[NN];

    // set default parameters
    if (num_parameter_sets > 0) {
        opts = (int**)malloc(sizeof(int*)* num_parameter_sets);
    } else {
        opts = (int**)malloc(sizeof(int*)* N);
    }
    int m = (num_parameter_sets>0)?
            num_parameter_sets:N;
    for(i=0;i<m;i++) {
        opts[i] = (int*)malloc(sizeof(int)*NUMOPTS);
        memcpy(opts[i], default_opts,sizeof(int)*NUMOPTS);
    }

    // seek for 'thread-private' parameter
    rewind(f);
    while(fgets(linebuf, 2048,f) != NULL)
    {
        char* l = linebuf;
        if ((l[0] == ' ') || (l[0] == '\t') || (l[0]=='\r')) 
             l=  strtok(linebuf, " \t\n\r");
        if (l==NULL) continue;
        if (l[0] == '#') continue;
        if (l[0] != '*') continue; 
        if (l[1] == '\0') continue;
        char pname[512];
        int tid; 
        int a1;
        sscanf(&l[1], "%s%d%d", pname, &tid, &a1);
        int p = parse_paramname(pname); 
        if (p == NUMOPTS) {
            printf("Ignoring unknown paramter:%s\n", pname);
            continue;
        }
        if ((p==NN) || (p==A1) || (p==A2) || (p==A3)) {
            printf("Warning: Ignoring non-privatizable parameter  %s, %d\n", pname, p);
            continue;
        }
        if (num_parameter_sets == 0) {
            if ((tid < 0) || (tid >= N)) {
                printf("Warning: Ignoring invalid thread-id  %d\n", tid);
                continue;
            }
        } else {
            if ((tid < 0) || (tid >= num_parameter_sets)) {
                printf("Warning: Ignoring invalid set-id  %d\n", tid);
                continue;
            }
        }
        opts[tid][p] = a1;
    }

    fclose(f);
}
void print_params(int N)
{
    int i,j;
    printf("[parameters]: %d threads\n", opts[0][NN]);

    int num_opts = (num_parameter_sets) ? NUMOPTS : NUMOPTS-1;
    for(i=0;i<num_opts;i++) {
        printf("%6s ", opt_names[i]);
    }
    printf("\n");
    int num_sets = (num_parameter_sets) ? num_parameter_sets : N;
    printf("----------------------------------------------------------------------------------------------------------------\n");
    for(j=0;j<num_sets;j++) {
        printf("%6d ", j);
        for(i=1;i<num_opts;i++) {
            printf("%6d ", opts[j][i]);
        }
        printf("\n");
    }
    printf("----------------------------------------------------------------------------------------------------------------\n");

}

//---------------------------------------------
// thread entry
//---------------------------------------------
void entry_fn(void* not_used)
{
    int tid = thread_getId();
    unsigned seed = (global_seed ) * tid;
    TM_THREAD_ENTER();
    if (num_parameter_sets == 0)
    {
        eigenbench_core(TM_ARG tid, &seed, opts[tid]);
    } else
    {
        // set up number of parameter sets
        int total = 0;
        int* remain = (int*) malloc(sizeof(int)*num_parameter_sets);
        int i;
        for(i=0;i<num_parameter_sets;i++) {
            remain[i] = opts[i][M]/num_threads;
            total += remain[i];
        }

        while (total > 0) {
            // randomize set-selection 
            int dice = uniform(&seed, total, 0);
            int j;
            for(j=0;j<num_parameter_sets;j++)
            {
                if (remain[j]==0) continue;
                if (dice < remain[j])
                {
                    break;
                }
                dice -= remain[j];
            }
            assert(j<num_parameter_sets);
            remain[j]--;
            total--;

            // execute one set
            eigenbench_core(TM_ARG tid, &seed, opts[j]);
        }
    }

    TM_THREAD_EXIT();
}

//---------------------------------------------
// main entry
//---------------------------------------------
int main(int argc, char** argv)
{
  time_t seconds;
  time(&seconds);
  global_seed = (uint)seconds ;
  int opt;
  int printopt = 0;

  // parse command-line options
  while ((opt = getopt(argc, argv, "ps:")) != -1) {
    switch(opt) {
    case 'p':
        printopt = 1;
        break;
    case 's':
      global_seed = atoi(optarg);
      break;
    default:
      fprintf(stderr, "Usage: %s [-p] [-s seed] test_file\n",argv[0]);
      fprintf(stderr, "\t -p: print parameters\n");
      exit(EXIT_FAILURE);
    }
  }
  if (optind >= argc) {
      fprintf(stderr, "Usage: %s [-p] [-s seed] test_file\n", argv[0]);
      fprintf(stderr, "\t -p: print parameters\n");
      exit(EXIT_FAILURE);
  }
  
  parse_paramfile(argv[optind]);
  int N = opts[0][NN];

  if (printopt)
    print_params(N);

  // Main Execution
  eigenbench_init_arrays(opts[0][NN], opts[0][A1], 
          opts[0][A2], opts[0][A3]);

  TM_STARTUP();
  thread_startup(N);
  fflush(stdout);

  struct timeval start, stop;
  gettimeofday(&start, NULL);

  thread_start(entry_fn, NULL);

  gettimeofday(&stop, NULL);
  thread_shutdown();
  TM_SHUTDOWN();

  printf("execution time = %lf (ms)\n", (stop.tv_sec - start.tv_sec) * 1000 + (stop.tv_usec - start.tv_usec)*0.001);
  // printf("Elapsed time: %0.6lf\n",
  //                   (((double)(stop.tv_sec)  + (double)(stop.tv_usec / 1000000.0)) - \
  //                   ((double)(start.tv_sec) + (double)(start.tv_usec / 1000000.0))));





  eigenbench_free_arrays(); 

}
