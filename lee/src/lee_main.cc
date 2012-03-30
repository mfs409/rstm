/*
 * BSD License
 *
 * Copyright (c) 2007, The University of Manchester (UK)
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     - Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     - Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     - Neither the name of the University of Manchester nor the names
 *       of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.

 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <iostream>
#include <pthread.h>
#include <getopt.h>
#include <ctime>
#include <sys/time.h>
#include <setjmp.h>
#include <cstdlib>

#include "tm.h"
#include "lee.h"
#include "timer.h"

///////////////
// constants //
///////////////

#define MILLISECONDS_IN_SECOND 1000
#define MICROSECONDS_IN_MILLISECOND 1000

#define MAX_THREADS 32

#ifdef IRREGULAR_ACCESS_PATTERN
#define IRREGULAR_WRITE_RATIO 20
#define IRREGULAR_READ_RATIO 100
#endif // IRREGULAR_ACCESS_PATTERN

int ***buf[MAX_THREADS];


/////////////////////
// data structures //
/////////////////////

struct command_line_args {
	char *input_file_name;
	
	unsigned thread_count;
};


/////////////////
// global data //
/////////////////

command_line_args cmdl_args;



///////////////////////////
// function declarations //
///////////////////////////

void parse_arguments(command_line_args &args, char **argv, int argc);

void print_arguments(command_line_args &args);

void print_help();

void run_benchmark(void);


int ***create_private_buffer(Lee *lee);

void run_transactions(void *arg);

long get_time_ms();

#ifdef IRREGULAR_ACCESS_PATTERN
bool should_irregular_write(unsigned *seed);
bool should_irregular_read(unsigned *seed);
#endif // IRREGULAR_ACCESS_PATTERN

static Lee *lee = NULL;

//////////////////////////
// function definitions //
//////////////////////////

int main(int argc, char **argv) {
	parse_arguments(cmdl_args, argv, argc);
	print_arguments(cmdl_args);
	
	run_benchmark();
}

void parse_arguments(command_line_args &args, char **argv, int argc) {
	static struct option long_options[] = {
		{"input_file", 1, 0, 'f'},
		{"help", 0, 0, '?'},
		{"threads", 1, 0, 't'},
		{0, 0, 0, 0}
	};

	// set defaults
	args.thread_count = 1;
	args.input_file_name = NULL;

	// parse command line
	while(true) {
		int option_index;
		int c = getopt_long(argc, argv, "f:?t:",
			long_options, &option_index);

		if(c == -1) {
			break;
		}

		switch(c) {
			case '?':
				print_help();
				exit(0);
			break;
			case 'f':
				args.input_file_name = optarg;
			break;
			case 't':
				args.thread_count = atoi(optarg);
			break;
		}
	}

	if(args.input_file_name == NULL) {
		print_help();
		exit(1);
	}
}

void print_help() {
	std::cout << "lee -f file_name [-t thread_cnt]" << std::endl;
}

void print_arguments(command_line_args &args) {
	std::cout << std::endl;
	std::cout << "Parameters:" << std::endl;
	std::cout << "===========" << std::endl;
	std::cout << "input file: " << args.input_file_name << std::endl;
	std::cout << "threads: " << args.thread_count << std::endl;
	std::cout << std::endl;
}

void run_benchmark() {
	int i;
	TIMER_T start;
	TIMER_T stop;

	// initialize barriers
	thread_startup(cmdl_args.thread_count);

	// initialize global stm
	TM_STARTUP(cmdl_args.thread_count);

	// create Lee benchmark
	lee = new Lee(cmdl_args.input_file_name, false, false, false);

	for (i = 0; i < cmdl_args.thread_count; i++) {
           buf[i] = create_private_buffer(lee);
        }

 	TIMER_READ(start);

	// initialize thread arguments
	thread_start(&run_transactions, NULL);

	TIMER_READ(stop);

        TM_SHUTDOWN();


	printf("Time = %0.6lf\n",
           TIMER_DIFF_SECONDS(start, stop));
#if 0

	// run one instance in this thread
	long duration_ms = (long)thread_main(targs);
	float duration_s = (float)duration_ms / MILLISECONDS_IN_SECOND;

	// print statistics
	std::cout << "Duration: " << duration_ms << " ms" << std::endl;

	unsigned commits = 0;
	unsigned aborts = 0;

	for(unsigned i = 0;i < cmdl_args.thread_count;i++) {
		commits += targs[i].commits;
		aborts += targs[i].aborts;
	}

	std::cout << "Total commits: " << commits << std::endl;
	std::cout << "Total commits per second: " << commits / duration_s << std::endl;
	std::cout << "Total aborts: " << aborts << std::endl;
	std::cout << "Total aborts per second: " << aborts / duration_s << std::endl;
#endif
}


void run_transactions(void *targ) {
        TM_THREAD_ENTER();
        unsigned seed = thread_getId();
        int ***private_buffer = buf[thread_getId()]; //create_private_buffer(lee);


	while(true) {
		WorkQueue *track = lee->getNextTrack();

		if(track == NULL) {
			break;
		}

		TM_BEGIN();

#ifdef IRREGULAR_ACCESS_PATTERN
		// perform an update or read of contention object
		if(should_irregular_write(&seed)) {
			lee->update_contention_object();
		} else if(should_irregular_read(&seed)) {
			lee->read_contention_object();
		}
#endif // IRREGULAR_ACCESS_PATTERN

		// transaction body
		lee->layNextTrack(track, private_buffer);

		// end transaction
		TM_END();

	}
        TM_THREAD_EXIT();
}

int ***create_private_buffer(Lee *lee) {
	Grid *grid = lee->grid;
	int ***ret = (int ***)malloc(sizeof(ret) * grid->getWidth());

	for(int i = 0;i < grid->getWidth();i++) {
		ret[i] = (int **)malloc(sizeof(ret[0]) * grid->getHeight());

		for(int k = 0;k < grid->getHeight();k++) {
			ret[i][k] = (int *)malloc(sizeof(ret[0][0]) * grid->getDepth());
		}
	}

	return ret;
}

long get_time_ms() {
	struct timeval t;
	::gettimeofday(&t, NULL);
	return t.tv_sec * MILLISECONDS_IN_SECOND +
		t.tv_usec / MICROSECONDS_IN_MILLISECOND;
}

#ifdef IRREGULAR_ACCESS_PATTERN
bool should_irregular_write(unsigned *seed) {
	unsigned percent = rand_r(seed) % 100;
	return percent <= IRREGULAR_WRITE_RATIO;
}


bool should_irregular_read(unsigned *seed) {
	unsigned percent = rand_r(seed) % 100;
	return percent <= IRREGULAR_READ_RATIO;
}
#endif // IRREGULAR_ACCESS_PATTERN
