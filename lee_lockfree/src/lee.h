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

#ifndef __LEE_H__
#define __LEE_H__

#include <string>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <list>
#include <pthread.h>

#include "tm.h"

class GridCell
{
    int m_val;
	
	public:
        // ctor
        GridCell(int val = -1)
            : m_val(val)
        { }

		void set_val(int v) {
			stm::TxThread *tx = stm::Self;
			TM_WRITE(m_val, v);
		}

		int get_val() {
			stm::TxThread *tx = stm::Self;
			return (int)TM_READ(m_val);
		}
};

class Frontier {

	int x, y, z, dw;

	public:
	Frontier(int xx, int yy, int zz, int ddw);
	int getDw();
	void setDw(int ddww);
	int getX();
	void setX(int xx);
	int getY();
	void setY(int yy);
	int getZ();
	void setZ(int zz);
};
    
    
	class Grid {
		
		public:
		int width, height, depth;
		bool releasable;
		GridCell ***grid;
		GridCell ***verifyGrid;
		int debugCount;
		int divisor;
		
		static const int OCC   = 5120;
		static const int VIA   = 6000;
		static const int BVIA  = 6001;
		static const int TRACK = 8192;
		static const int MAX_WEIGHT = 1;
		
		Grid(int gridWidth, int gridHeight, int gridDepth, bool rel);
		void addWeights();
		void occupy(int loX, int loY, int upX, int upY);
		int getWidth();
		int getHeight();
		int getDepth();
		int getPoint(int x, int y, int z);
		void setPoint(int x, int y, int z, int val);
		int getPointNonRelease(int x, int y, int z);
		int getVerifyPoint(int x, int y, int z);
		void setVerifyPoint(int x, int y, int z, int val);
		int getVerifyPointNonRelease(int x, int y, int z);
		void instantiateGrid(GridCell ***g);
		void resetGrid(GridCell ***g);
		bool isValidTrackID(int i);
		void printLayout(bool toFile);
		
	};
	
	class WorkQueue {
		public:
		int x1, y1, x2, y2, netNo;
		long priority;
		double lengthSquared;
		WorkQueue *next;
		
		public:
		WorkQueue();
		WorkQueue(int xs, int ys, int xg, int yg, int nn);
		int getX1();
		int getY1();
		int getX2();
		int getY2();
		int getNetNo();
		void sort();
		void enQueue(int xs, int ys, int xg, int yg, int nn);
		void enQueue(WorkQueue *q);
		WorkQueue * deQueue();
		void toString();
		WorkQueue * getNext();
		void setNext(WorkQueue *q);
		bool equals(WorkQueue *q);
		int listLength();
		bool less(int xx1, int yy1, int xx2, int yy2);
		bool less(WorkQueue *n);
		
		
	};

	struct thread_args_t {
		int id;
	};

#ifdef IRREGULAR_ACCESS_PATTERN
	class ContentionObject {
		public:
		    // ctor
	        ContentionObject(int val = -1)
	            : m_val(val)
	        { }
	
			int get_val() const {
				return (int)tm_read_word((void *)&m_val);
			}

			void update_val() {
				int val = (int)tm_read_word(&m_val);
				tm_write_word((void *)&m_val, (void *)(val + 1));
			}

		private:
			int m_val;
	};

#endif // IRREGULAR_ACCESS_PATTERN


	class Lee {
	
		public:
	
	    static const bool TEST = false;
	    //static const bool DEBUG = true;
	    static const bool DEBUG = false;
	    int GRID_SIZE;
	    static const bool XML_REPORT = false;
	    static const bool VERIFY = true;
	    static const int EMPTY = 0;
	    static const int TEMP_EMPTY = 10000;

	    // note these very useful arrays
		static const int dx[2][4];
		static const int dy[2][4];

		//static int ***private_work_area[lpdstm::MAX_THREADS];

	    int netNo, num_vias, forced_vias, failures, maxTrackLength;
	    Grid *grid;
	    WorkQueue *work, *verifyQueue;
	    pthread_mutex_t queueLock, verifyLock;
	    Lee();
	    Lee(const char* file, bool test, bool debug, bool rel);
	    void parseDataFile(const char *file);
	    void fakeTestData();
	    int readInt(std::string *line);
	    WorkQueue* getNextTrack();
	    void addTrackForVerification(WorkQueue *q);
		void removeTrackFromVerification(WorkQueue *q);
		bool ok(int x, int y);
		int deviation(int x1, int y1, int x2, int y2);
		bool expandFromTo(int x, int y, int xGoal, int yGoal,
		int num, int ***tempg);
		bool backtrackFrom(int xGoal, int yGoal, int xStart,
		int yStart, int trackNo, int ***tempg);
		int trackLength(int x1, int y1, int x2, int y2);
		bool pathFromOtherSide(int ***g, int X, int Y, int Z);
		bool connect(WorkQueue *q, int ***tempg); 
		bool layNextTrack(WorkQueue *q, int ***tempg);

#ifdef IRREGULAR_ACCESS_PATTERN
		ContentionObject contention_object;

		unsigned read_contention_object() {
			return contention_object.get_val();
		}

		void update_contention_object() {
			contention_object.update_val();
		}
#endif // IRREGULAR_ACCESS_PATTERN

	};
	
	
#endif /*__LEE_H__*/
