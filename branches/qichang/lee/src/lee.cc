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

#include <string>
#include <vector>
#include <list>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <math.h>
#include <cstdlib>

#include "lee.h"

using namespace std;

const int Lee::dx[2][4] = { { -1, 1, 0, 0 },
						  { 0, 0, -1, 1 } };
const int Lee::dy[2][4] = { { 0, 0, -1, 1 },
						  { -1, 1, 0, 0 } };

Lee::Lee(const char* file, bool test, bool debug, bool rel)
{ 
	netNo = num_vias = forced_vias = failures = 0;
	pthread_mutex_init(&queueLock, NULL);
	pthread_mutex_init(&verifyLock, NULL);
    if (TEST) GRID_SIZE = 10;
    else GRID_SIZE = 600;
    maxTrackLength = ((GRID_SIZE + GRID_SIZE) / 2) * 5; //Extra work in case grid is not square
    if(DEBUG) printf("Creating grid...\n");
    grid = new Grid(GRID_SIZE, GRID_SIZE, 2, rel); //the Lee 3D Grid;

    printf("grid: %d\n", (intptr_t)grid);

    if(DEBUG) printf("Done creating grid\n");
    work = new WorkQueue; // empty
    if(DEBUG) printf("Parsing data...\n");
//    BEGIN_TRANSACTION;

    TM_THREAD_ENTER();
    TM_BEGIN();

    if (!TEST) parseDataFile(file);
    else fakeTestData(); //WARNING: Needs grid at least 10x10x2
    if(DEBUG) printf("Done parsing data\n");
    if(DEBUG) printf("Adding weights... \n");
    //grid->addWeights();

    TM_END();

//	END_TRANSACTION;
    if(DEBUG) printf("Done adding weights\n");
    work->sort();
    if(DEBUG) work->toString();
    
    if(VERIFY)
        verifyQueue = new WorkQueue;
    else
        verifyQueue = NULL;

    TM_THREAD_EXIT();
}

void Lee::parseDataFile(const char *file) {
	// Read very simple HDL file
	ifstream inputFile;
	inputFile.open(file);
	if(!inputFile.is_open()) {
		printf("Unable to open %s\n", file);
		exit(-1);
	}
	
	int i = 0;
	string line;
	while (!inputFile.eof()) {
		getline (inputFile, line);
		char c = line.at(0);
		line = line.substr(1, line.length()-1);
		if (c == 'E')
			break; // end of file
		if (c == 'C') // chip bounding box
		{
			int x0 = readInt(&line);
			int y0 = readInt(&line);
			int x1 = readInt(&line);
			int y1 = readInt(&line);
			grid->occupy(x0, y0, x1, y1);
		}
		if (c == 'P') // pad
		{
			int x0 = readInt(&line);
			int y0 = readInt(&line);
			grid->occupy(x0, y0, x0, y0);
		}
		if (c == 'J') // join connection pts
		{
			i++;
			int x0 = readInt(&line);
			int y0 = readInt(&line);
			int x1 = readInt(&line);
			int y1 = readInt(&line);
			netNo++;
			work->enQueue(x0, y0, x1, y1, netNo);
		}
	}
	inputFile.close();
	
}

int Lee::readInt(string *input_line) {
	unsigned int linepos = 0;
	while ((input_line->at(linepos) == ' ')
			|| (input_line->at(linepos) == '\t'))
		linepos++;
	unsigned int fpos = linepos;
	while ((linepos < input_line->length())
			&& (input_line->at(linepos) != ' ')
			&& (input_line->at(linepos) != '\t'))
		linepos++;
	string temp = input_line->substr(fpos, linepos);
	int n = strtol(&(temp[0]), NULL, 10);//input_line.substr(fpos, linepos-fpos).c_str());
	*input_line = input_line->substr(linepos, input_line->length()-linepos);
	
	return n;
	
}

void Lee::fakeTestData() {
		netNo++;
		grid->occupy(7, 3, 7, 3);grid->occupy(7, 7, 7, 7);
		work->enQueue(7, 3, 7, 7, netNo);
		
		netNo++;
		grid->occupy(3, 6, 3, 6);grid->occupy(8, 6, 8, 6);
		work->enQueue(3, 6, 8, 6, netNo);
		
		netNo++;
		grid->occupy(5, 3, 5, 3);grid->occupy(8, 5, 8, 5);
		work->enQueue(5, 3, 8, 5, netNo);
		
		netNo++;
		grid->occupy(8, 3, 8, 3);grid->occupy(2, 6, 2, 6);
		work->enQueue(8, 3, 2, 6, netNo);
		
		netNo++;
		grid->occupy(4, 3, 4, 3);grid->occupy(6, 7, 6, 7);
		work->enQueue(4, 3, 6, 7, netNo);
		
		netNo++;
		grid->occupy(3, 8, 3, 8);grid->occupy(8, 3, 8, 3);
		work->enQueue(3, 8, 8, 3, netNo);
	
}

WorkQueue* Lee::getNextTrack() {
	WorkQueue *retval = NULL;
	pthread_mutex_lock(&queueLock);
	if(work->getNext() != NULL) {
		if(DEBUG) printf("Tracks remaining: %d\n", work->listLength());
		retval =  work->deQueue();
	}
	pthread_mutex_unlock(&queueLock);
	return retval;
}

void Lee::addTrackForVerification(WorkQueue *q) {
	pthread_mutex_lock(&verifyLock);
	if(VERIFY) verifyQueue->enQueue(q);
	pthread_mutex_unlock(&verifyLock);
}
	
void Lee::removeTrackFromVerification(WorkQueue *q) {
	pthread_mutex_lock(&verifyLock);
	WorkQueue *curr = verifyQueue;
	while(curr->getNext()!=NULL) {
		if(curr->getNext()->equals(q)) {
			curr->setNext(curr->getNext()->getNext());
			break;
		}
		else {
			curr = curr->getNext();
		}
	}
	pthread_mutex_unlock(&verifyLock);
}

bool Lee::ok(int x, int y) {
	// checks that point is actually within the bounds
	// of grid array
	return (x > 0 && x < grid->getWidth() - 1 && y > 0 && y < grid->getHeight() - 1);
}

int Lee::deviation(int x1, int y1, int x2, int y2) {
	int xdiff = x2 - x1;
	int ydiff = y2 - y1;
	if (xdiff < 0)
		xdiff = -xdiff;
	if (ydiff < 0)
		ydiff = -ydiff;
	if (xdiff < ydiff)
		return xdiff;
	else
		return ydiff;
}


bool Lee::connect(WorkQueue *q, int ***tempg) {
	int xs = q->getX1();
	int ys = q->getY1();
	int xg = q->getX2();
	int yg = q->getY2();
	int netNo = q->getNetNo();
	if(Lee::DEBUG) printf("Connecting %d %d %d %d %d\n", xs, ys, xg, yg, netNo);
	// calls expandFrom and backtrackFrom to create connection		
	// This is the only real change needed to make the program
	// transactional.
	// Instead of using the grid 'in place' to do the expansion, we take a
	// copy
	// but the backtrack writes to the original grid.
	// This is not a correctness issue. The transactions would still
	// complete eventually without it.
	// However the expansion writes are only temporary and do not logically
	// conflict.
	// There is a question as to whether a copy is really necessary as a
	// transaction will anyway create
	// its own copy. if we were then to distinguish between writes not to be
	// committed (expansion) and
	// those to be committed (backtrack), we would not need an explicit
	// copy.
	// Taking the copy is not really a computational(time) overhead because
	// it avoids the grid 'reset' phase
	// needed if we do the expansion in place.
	for (int x = 0; x < grid->getWidth(); x++) {
		for (int y = 0; y < grid->getHeight(); y++) {
			for (int z = 0; z < grid->getDepth(); z++)
				tempg[x][y][z] = TEMP_EMPTY;
		}
	}
	bool success = true;
	// call the expansion method to return found/not found boolean
	if(Lee::DEBUG) printf("Performing expansion for %d\n", netNo);
	bool found = expandFromTo(xs, ys, xg, yg, maxTrackLength * 5, tempg); 
	if (found) {
		if(Lee::DEBUG) printf("Target (%d,%d) FOUND!\n", xg, yg);
		success = backtrackFrom(xg, yg, xs, ys, netNo, tempg); // call the
		// backtrack method
		if(success && Lee::VERIFY) {
			addTrackForVerification(q);
		}
	} // print outcome of expansion method
	else {
		if(DEBUG) printf("Failed to route %d %d to %d %d\n", xs, ys, xg, yg);
		failures++;
	}
	return success;
}


bool Lee::backtrackFrom(int xGoal, int yGoal, int xStart,
		int yStart, int trackNo, int ***tempg) {
	// this method should backtrack from the goal position (xGoal, yGoal)
	// back to the starting position (xStart, yStart) filling in the
	// grid array g with the specified track number trackNo ( + TRACK).

	// ***
	// CurrentPos = Goal
	// Loop
	// Find dir to start back from current position
	// Loop
	// Keep going in current dir and Fill in track (update currentPos)
	// Until box number increases in this current dir
	// Until back at starting point
	// ***
	if(Lee::DEBUG) printf("Track %d backtrack length %d\n", trackNo,
					 trackLength(xStart, yStart, xGoal, yGoal));
	int zGoal;
	int distsofar = 0;
	if (fabs(xGoal - xStart) > fabs(yGoal - yStart))
		zGoal = 0;
	else
		zGoal = 1;
	if (tempg[xGoal][yGoal][zGoal] == TEMP_EMPTY) {
		if(DEBUG) printf("Preferred Layer not reached %d\n", zGoal);
		zGoal = 1 - zGoal;
	}
	int tempY = yGoal;
	int tempX = xGoal;
	int tempZ = zGoal;
	int lastdir = -10;
	while ((tempX != xStart) || (tempY != yStart)) { // PDL: until back
		
		// at starting point
		bool advanced = false;
		int mind = 0;
		int dir = 0;
		int min_square = 100000;
		int d;
		for (d = 0; d < 4; d++) { // PDL: Find dir to start back from
			// current position
			if ((tempg[tempX + dx[tempZ][d]][tempY + dy[tempZ][d]][tempZ] < tempg[tempX][tempY][tempZ])
					&& (tempg[tempX + dx[tempZ][d]][tempY + dy[tempZ][d]][tempZ] != TEMP_EMPTY)) {
				if (tempg[tempX + dx[tempZ][d]][tempY + dy[tempZ][d]][tempZ] < min_square) {
					min_square = tempg[tempX + dx[tempZ][d]][tempY
							+ dy[tempZ][d]][tempZ];
					mind = d;
					dir = dx[tempZ][d] * 2 + dy[tempZ][d]; // hashed dir
					if (lastdir < -2)
						lastdir = dir;
					advanced = true;
				}
			}
		}
		
		if (advanced)
			distsofar++;
		if(DEBUG) 
			printf("Backtracking %d %d %d %d %d %d\n", tempX, tempY, tempZ,
					tempg[tempX][tempY][tempZ], advanced, mind);
		if (pathFromOtherSide(tempg, tempX, tempY, tempZ)
				&& ((mind > 1)
						&& // not preferred dir for this layer
						(distsofar > 15)
						&& (trackLength(tempX, tempY, xStart, yStart) > 15) ||
				// (deviation(tempX,tempY,xStart,yStart) > 3) ||
				(!advanced && ((grid->getPointNonRelease(tempX,tempY,tempZ) != Grid::VIA) 
						&& (grid->getPointNonRelease(tempX,tempY,tempZ) != Grid::BVIA))))) {
			int tZ = 1 - tempZ; // 0 if 1, 1 if 0
			int viat;
			if (advanced)
				viat = Grid::VIA;
			else
				viat = Grid::BVIA; // BVIA is nowhere else to go
			//This get point is on purpose for release-based code
			if(grid->getPointNonRelease(tempX,tempY,tempZ)>Grid::OCC){
				return false;
			}
			int tempVal;
			if(VERIFY) {
				tempVal = grid->getVerifyPointNonRelease(tempX, tempY, tempZ);
				if(tempVal!=0 && tempVal<Grid::OCC) {
					return false;
				}
			}
			// mark via
			tempg[tempX][tempY][tempZ] = viat;
			grid->setPoint(tempX,tempY,tempZ,Grid::TRACK+trackNo);
			if(VERIFY)grid->setVerifyPoint(tempX,tempY,tempZ,trackNo);
			tempZ = tZ;
			//This get point is on purpose for release-based code
			if(grid->getPointNonRelease(tempX,tempY,tempZ)>Grid::OCC) {
				return false;
			}
			if(VERIFY) {
				tempVal = grid->getVerifyPointNonRelease(tempX, tempY, tempZ);
				if(tempVal!=0 && tempVal<Grid::OCC) {
					return false;
				}
			}
			// and the other side
			tempg[tempX][tempY][tempZ] = viat;
			grid->setPoint(tempX,tempY,tempZ,Grid::TRACK+trackNo);
			if(VERIFY)grid->setVerifyPoint(tempX,tempY,tempZ,trackNo);
			num_vias++;
			if (!advanced)
				forced_vias++;
			if (advanced)
				if(DEBUG)
					printf("Via %d %d %d\n", distsofar,
						trackLength(tempX, tempY, xStart, yStart),
						deviation(tempX, tempY, xStart, yStart));
			distsofar = 0;
		} else {
			//This get point is on purpose for release-based code
			int tempVal = grid->getPointNonRelease(tempX,tempY,tempZ);
			if (tempVal < Grid::OCC) {
				 // PDL: fill in track unless connection point
				grid->setPoint(tempX,tempY,tempZ,Grid::TRACK+trackNo);
				if(VERIFY)grid->setVerifyPoint(tempX,tempY,tempZ,trackNo);
			} 
			else if (tempVal == Grid::OCC) {
				if(VERIFY)grid->setVerifyPoint(tempX,tempY,tempZ,Grid::OCC);
				if(VERIFY)grid->setVerifyPoint(tempX,tempY,1-tempZ,Grid::OCC);
			}
			else if (tempVal > Grid::OCC && tempVal != Grid::TRACK+trackNo) {
				return false;
			}
			tempX = tempX + dx[tempZ][mind]; // PDL: updating current
			// position on x axis
			tempY = tempY + dy[tempZ][mind]; // PDL: updating current
			// position on y axis
		}
		lastdir = dir;
	}
	if(Lee::DEBUG) printf("Track %d completed\n", trackNo);
	return true;
}







bool Lee::expandFromTo(int x, int y, int xGoal, int yGoal,
		int num, int ***tempg) {
	// this method should use Lee's expansion algorithm from
	// coordinate (x,y) to (xGoal, yGoal) for the num iterations
	// it should return true if the goal is found and false if it is not
	// reached within the number of iterations allowed.

	// g[xGoal][yGoal][0] = EMPTY; // set goal as empty
	// g[xGoal][yGoal][1] = EMPTY; // set goal as empty
	vector<Frontier> front;
	vector<Frontier> tmp_front;
	tempg[x][y][0] = 1; // set grid (x,y) as 1
	tempg[x][y][1] = 1; // set grid (x,y) as 1
	bool trace1 = false;
	front.push_back(Frontier(x, y, 0, 0));
	front.push_back(Frontier(x, y, 1, 0)); // we can start from either
	// side
	if(Lee::DEBUG) printf("Expanding %d + %d + %d + %d\n", x, y, xGoal, yGoal);
	int extra_iterations = 50;
	bool reached0 = false;
	bool reached1 = false;
	while (!front.empty()) {
		while (!front.empty()) {
			int weight, prev_val;
			Frontier f = (Frontier) front.at(0);
			vector<Frontier>::iterator startIterator;
			startIterator = front.begin();
    		front.erase( startIterator );
			if (f.getDw() > 0) {
				tmp_front.push_back(Frontier(f.getX(), f.getY(), f.getZ(), f.getDw() - 1));
			} else {
				if (trace1)
					if(Lee::DEBUG) 
					printf("X %d Y %d Z %d DW %d processing - val %d\n", f.getX(), f.getY(),
							f.getZ(), f.getDw(), tempg[f.getX()][f.getY()][f.getZ()]);
				weight = grid->getPoint(f.getX(),f.getY() + 1,f.getZ()) + 1;
				prev_val = tempg[f.getX()][f.getY() + 1][f.getZ()];
				bool reached = (f.getX() == xGoal) && (f.getY() + 1 == yGoal);
				if ((prev_val > tempg[f.getX()][f.getY()][f.getZ()] + weight)
						&& (weight < Grid::OCC) || reached) {
					if (ok(f.getX(), f.getY() + 1)) {
						tempg[f.getX()][f.getY() + 1][f.getZ()] = tempg[f.getX()][f.getY()][f.getZ()]
								+ weight; // looking north
						if (!reached)
							tmp_front.push_back(Frontier(f.getX(), f.getY() + 1,
									f.getZ(), 0));
					}
				}
				weight = grid->getPoint(f.getX() + 1,f.getY(),f.getZ()) + 1;
				prev_val = tempg[f.getX() + 1][f.getY()][f.getZ()];
				reached = (f.getX() + 1 == xGoal) && (f.getY() == yGoal);
				if ((prev_val > tempg[f.getX()][f.getY()][f.getZ()] + weight)
						&& (weight < Grid::OCC) || reached) {
					if (ok(f.getX() + 1, f.getY())) {
						tempg[f.getX() + 1][f.getY()][f.getZ()] = tempg[f.getX()][f.getY()][f.getZ()]
								+ weight; // looking east
						if (!reached)
							tmp_front.push_back(Frontier(f.getX() + 1, f.getY(),
									f.getZ(), 0));
					}
				}
				weight = grid->getPoint(f.getX(),f.getY() - 1,f.getZ()) + 1;
				prev_val = tempg[f.getX()][f.getY() - 1][f.getZ()];
				reached = (f.getX() == xGoal) && (f.getY() - 1 == yGoal);
				if ((prev_val > tempg[f.getX()][f.getY()][f.getZ()] + weight)
						&& (weight < Grid::OCC) || reached) {
					if (ok(f.getX(), f.getY() - 1)) {
						tempg[f.getX()][f.getY() - 1][f.getZ()] = tempg[f.getX()][f.getY()][f.getZ()]
								+ weight; // looking south
						if (!reached)
							tmp_front.push_back(Frontier(f.getX(), f.getY() - 1,
									f.getZ(), 0));
					}
				}
				weight = grid->getPoint(f.getX() - 1,f.getY(),f.getZ()) + 1;
				prev_val = tempg[f.getX() - 1][f.getY()][f.getZ()];
				reached = (f.getX() - 1 == xGoal) && (f.getY() == yGoal);
				if ((prev_val > tempg[f.getX()][f.getY()][f.getZ()] + weight)
						&& (weight < Grid::OCC) || reached) {
					if (ok(f.getX() - 1, f.getY())) {
						tempg[f.getX() - 1][f.getY()][f.getZ()] = tempg[f.getX()][f.getY()][f.getZ()]
								+ weight; // looking west
						if (!reached)
							tmp_front.push_back(Frontier(f.getX() - 1, f.getY(),
									f.getZ(), 0));
					}
				}
				if (f.getZ() == 0) {
					weight = grid->getPoint(f.getX(),f.getY(),1) + 1;
					if ((tempg[f.getX()][f.getY()][1] > tempg[f.getX()][f.getY()][0])
							&& (weight < Grid::OCC)) {
						tempg[f.getX()][f.getY()][1] = tempg[f.getX()][f.getY()][0];
						tmp_front.push_back(Frontier(f.getX(), f.getY(), 1, 0));
					}
				} else {
					weight = grid->getPoint(f.getX(),f.getY(),0) + 1;
					if ((tempg[f.getX()][f.getY()][0] > tempg[f.getX()][f.getY()][1])
							&& (weight < Grid::OCC)) {
						tempg[f.getX()][f.getY()][0] = tempg[f.getX()][f.getY()][1];
						tmp_front.push_back(Frontier(f.getX(), f.getY(), 0, 0));
					}
				}
				// must check if found goal, if so return TRUE
				reached0 = tempg[xGoal][yGoal][0] != TEMP_EMPTY;
				reached1 = tempg[xGoal][yGoal][1] != TEMP_EMPTY;
				if ((reached0 && !reached1) || (!reached0 && reached1))
					extra_iterations = 100;
				if ((extra_iterations == 0) && (reached0 || reached1)
						|| (reached0 && reached1)) {
					return true; // if (xGoal, yGoal) can be found in
					// time
				} else
					extra_iterations--;
			}
		}
		vector<Frontier> tf;
		tf = front;
		front = tmp_front;
		tmp_front = tf;
	}
//		 view.pad(x,y,red);
//		 view.pad(xGoal,yGoal,red);
	return false;
}


bool Lee::pathFromOtherSide(int ***g, int X, int Y, int Z) {
	bool ok;
	int Zo;
	Zo = 1 - Z; // other side
	int sqval = g[X][Y][Zo];
	if ((sqval == Grid::VIA) || (sqval == Grid::BVIA))
		return false;
	ok = (g[X][Y][Zo] <= g[X][Y][Z]);
	if (ok)
		ok = (g[X - 1][Y][Zo] < sqval) || (g[X + 1][Y][Zo] < sqval)
				|| (g[X][Y - 1][Zo] < sqval) || (g[X][Y + 1][Zo] < sqval);
	return ok;
}

int Lee::trackLength(int x1, int y1, int x2, int y2) {
	int sq = (x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1);
	return (int) sqrt((double) sq);
}



bool Lee::layNextTrack(WorkQueue *q, int ***tempg) {
	// start transaction
	return connect(q, tempg);
	// end transaction
}

Grid::Grid(int gridWidth, int gridHeight, int gridDepth, bool rel) {
	// Set up PCB grid
	releasable = rel;
	debugCount = 0;
	divisor = 500000;
	width = gridWidth;
	height = gridHeight;
	depth = gridDepth;
	if(Lee::DEBUG) printf("lpdstm::malloc grid\n");
	grid = (GridCell ***) malloc(sizeof(GridCell ***) * width);
	if(Lee::DEBUG) printf("instantiate grid\n");
	instantiateGrid(grid);
	printf("grid: %d\n", (intptr_t)grid);
	if(Lee::DEBUG) printf("reset grid\n");
//	resetGrid(grid); //Serves no purpose for C++ version

	if (Lee::VERIFY) {
		verifyGrid = (GridCell ***) malloc(sizeof(GridCell ***) * width);		
		instantiateGrid(verifyGrid);
//		resetGrid(verifyGrid); //Serves no purpose for C++ version
	} else {
		verifyGrid = NULL;
	}
	
}	


void Grid::resetGrid(GridCell ***g) {
	for (int i = 0; i < width; i++)
		for (int j = 0; j < height; j++)
			for (int k = 0; k < depth; k++) {
				g[i][j][k].set_val(Lee::EMPTY);
			}
}

void Grid::instantiateGrid(GridCell ***g) {
	for (int i = 0; i < width; i++) {
		g[i] = (GridCell **)malloc(sizeof(GridCell **) * height);

		for (int j = 0; j < height; j++) {
			g[i][j] = (GridCell *)malloc(sizeof(GridCell *) * depth);

			for (int k = 0; k < depth; k++) {
				g[i][j][k] = GridCell(Lee::EMPTY);
			}
		}
	}
}
	
void Grid::addWeights() {
	for (int i = 0; i < Grid::MAX_WEIGHT; i++) {
		for (int z = 0; z < depth; z++) {
			for (int x = 1; x < width - 1; x++) {
				for (int y = 1; y < height - 1; y++) {
					if (getPoint(x, y ,z) == Grid::OCC) {
						if (getPoint(x, y + 1, z) == Lee::EMPTY)
							setPoint(x, y + 1, z, Grid::MAX_WEIGHT);
						if (getPoint(x + 1, y, z) == Lee::EMPTY)
							setPoint(x + 1, y, z, Grid::MAX_WEIGHT);
						if (getPoint(x, y - 1, z) == Lee::EMPTY)
							setPoint(x, y - 1, z, Grid::MAX_WEIGHT);
						if (getPoint(x - 1, y, z) == Lee::EMPTY)
							setPoint(x - 1, y, z, Grid::MAX_WEIGHT);
					} else if (getPoint(x, y, z) != Lee::EMPTY) {
						if (getPoint(x, y + 1, z) == Lee::EMPTY)
							setPoint(x, y + 1, z, getPoint(x, y, z) - 1);
						if (getPoint(x + 1, y, z) == Lee::EMPTY)
							setPoint(x + 1, y, z, getPoint(x, y, z) - 1);
						if (getPoint(x, y - 1, z) == Lee::EMPTY)
							setPoint(x, y - 1, z, getPoint(x, y, z) - 1);
						if (getPoint(x - 1, y, z) == Lee::EMPTY)
							setPoint(x - 1, y, z, getPoint(x, y, z) - 1);
					}
				}
			}
		}
	}
}

bool Grid::isValidTrackID(int i) {
	return i > OCC;
}

void Grid::printLayout(bool toFile) {
	string fileName = "output.txt";

//	if (toFile)
//		ps = new PrintStream(new FileOutputStream(fileName));
//	else
//	ps = System.out;
	for (int k = 0; k < depth; k++) {
		for (int j = 0; j < height; j++) {
			for (int i = 0; i < width; i++) {
				int val = getPoint(i, j, k);
				if (isValidTrackID(val)) {
					if (Lee::VERIFY) {
						printf("%d\t", getVerifyPoint(i, j, k));
					}
					else {
						printf("X\t");
					}
				} else {
					if(val==OCC) {
						printf("X\t");
					} else {
						printf(".\t");
					}
				}
			}
			printf("\n");
		}
		printf("\n");
	}
}

void Grid::occupy(int loX, int loY, int upX, int upY) {
	int x = 0;
	int y = 0;
	for (x = loX; x <= upX; x++) {
		for (y = loY; y <= upY; y++) {
			for (int z = 0; z < depth; z++) {
				setPoint(x, y, z, Grid::OCC);
				if (Lee::VERIFY)
					setVerifyPoint(x, y, z, Grid::OCC);
			}
		}
	}
}

int Grid::getWidth() {
	return width;
}

int Grid::getHeight() {
	return height;
}

int Grid::getDepth() {
	return depth;
}

int Grid::getPoint(int x, int y, int z) {
	return grid[x][y][z].get_val();
}

void Grid::setPoint(int x, int y, int z, int val) {
	grid[x][y][z].set_val(val);
}

int Grid::getPointNonRelease(int x, int y, int z) {
	return grid[x][y][z].get_val();
}

void Grid::setVerifyPoint(int x, int y, int z, int val) {
	verifyGrid[x][y][z].set_val(val);
}

int Grid::getVerifyPoint(int x, int y, int z) {
	return verifyGrid[x][y][z].get_val();
}

int Grid::getVerifyPointNonRelease(int x, int y, int z) {
	return verifyGrid[x][y][z].get_val();
}

WorkQueue::WorkQueue() {
	next = NULL;	
}

WorkQueue::WorkQueue(int xs, int ys, int xg, int yg, int nn) {
	x1 = xs;
	y1 = ys;
	x2 = xg;
	y2 = yg;
	netNo = nn;
	lengthSquared = (x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2);
	priority = (long) sqrt(lengthSquared);
	
}

int WorkQueue::getX1() {
	return x1;
}

int WorkQueue::getY1() {
	return y1;
}

int WorkQueue::getX2() {
	return x2;
}

int WorkQueue::getY2() {
	return y2;
}

int WorkQueue::getNetNo() {
	return netNo;
}

void WorkQueue::sort() {
	bool done = false;
	while(!done) {
		done = true;
		WorkQueue *ent = this;
		WorkQueue *a = ent->next;
		while (a->next != NULL) {
			WorkQueue *b = a->next;
			if (a->less(b)) {
				ent->next = b;
				a->next = b->next;
				b->next = a;
				done = false;
			}
			ent = a;
			a = b;
			b = b->next;
		}
	}
	
}

bool WorkQueue::less(int xx1, int yy1, int xx2, int yy2) {
	return (x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1) > (xx2 - xx1)
			* (xx2 - xx1) + (yy2 - yy1) * (yy2 - yy1);
}

bool WorkQueue::less(WorkQueue *n) {
	return lengthSquared > n->lengthSquared;
}


void WorkQueue::enQueue(int xs, int ys, int xg, int yg, int nn) {
	WorkQueue *temp = new WorkQueue(xs, ys, xg, yg, nn);
	temp->next = next;
	next = temp;
}

void WorkQueue::enQueue(WorkQueue *q) {
	q->next = next;
	next = q;	
}

WorkQueue* WorkQueue::deQueue() {
	WorkQueue *q = next;
	next = next->next;
	return q;
}

int WorkQueue::listLength() {
	WorkQueue *curr = next;
	int retval = 0;
	
	while(curr!=NULL) {
		retval++;
		curr = curr->next;
	}
	
	return retval;
}

WorkQueue* WorkQueue::getNext() {
	return next;
}

void WorkQueue::setNext(WorkQueue *q) {
	next = q;
}

bool WorkQueue::equals(WorkQueue *q) {
	return q->netNo == netNo;
}

void WorkQueue::toString() {
	WorkQueue *temp = getNext();
	
	while(temp != NULL) {
		printf("Netno: %d x1:%d y1:%d x2:%d y2:%d\n", temp->netNo, temp->x1, temp->y1, temp->x2, temp->y2);
		temp = temp->next;	
	}
		
}

Frontier::Frontier(int xx, int yy, int zz, int ddw) {
	x = xx;
	y = yy;
	z = zz;
	dw = ddw;
}

int Frontier::getDw() {
	return dw;
}

void Frontier::setDw(int ddww) {
	dw = ddww;
}

int Frontier::getX() {
	return x;
}

void Frontier::setX(int xx) {
	x = xx;
}

int Frontier::getY() {
	return y;
}

void Frontier::setY(int yy) {
	y = yy;
}

int Frontier::getZ() {
	return z;
}

void Frontier::setZ(int zz) {
	z = zz;
}
