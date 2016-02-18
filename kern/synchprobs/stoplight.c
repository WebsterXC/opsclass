/*
 * Copyright (c) 2001, 2002, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Driver code is in kern/tests/synchprobs.c We will replace that file. This
 * file is yours to modify as you see fit.
 *
 * You should implement your solution to the stoplight problem below. The
 * quadrant and direction mappings for reference: (although the problem is, of
 * course, stable under rotation)
 *
 *   |0 |
 * -     --
 *    01  1
 * 3  32
 * --    --
 *   | 2|
 *
 * As way to think about it, assuming cars drive on the right: a car entering
 * the intersection from direction X will enter intersection quadrant X first.
 * The semantics of the problem are that once a car enters any quadrant it has
 * to be somewhere in the intersection until it call leaveIntersection(),
 * which it should call while in the final quadrant.
 *
 * As an example, let's say a car approaches the intersection and needs to
 * pass through quadrants 0, 3 and 2. Once you call inQuadrant(0), the car is
 * considered in quadrant 0 until you call inQuadrant(3). After you call
 * inQuadrant(2), the car is considered in quadrant 2 until you call
 * leaveIntersection().
 *
 * You will probably want to write some helper functions to assist with the
 * mappings. Modular arithmetic can help, e.g. a car passing straight through
 * the intersection entering from direction X will leave to direction (X + 2)
 * % 4 and pass through quadrants X and (X + 3) % 4.  Boo-yah.
 *
 * Your solutions below should call the inQuadrant() and leaveIntersection()
 * functions in synchprobs.c to record their progress.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

/*
 * Called by the driver during initialization.
 */

struct cv *wait_intersection;
struct lock *intersection_lockdown;
struct lock *lock0;
struct lock *lock1;
struct lock *lock2;
struct lock *lock3;

bool is_intersection_occupied;

/* HELPER FUNCTIONS */
struct lock * lock_i_need(unsigned int);


// Intent: pass modulus operation to return the specified lock
struct lock *
lock_i_need(unsigned int quadrant)
{
	switch(quadrant){
		
		case 0: return lock0;
		case 1: return lock1;
		case 2: return lock2;
		case 3: return lock3;	

	}
	
	return NULL;
}


/* SUPPLIED FUNCTIONS, REQUIRED FOR OPERATION */

void
stoplight_init() {
	
	// Create conditional to signify intersection is clear
	wait_intersection = cv_create("vehicle cv");
	intersection_lockdown = lock_create("biglock");

	// Create lock for each quadrant
	lock0 = lock_create("quadrant0");
	lock1 = lock_create("quadrant1");
	lock2 = lock_create("quadrant2");
	lock3 = lock_create("quadrant3");
	
	// Intersection needs to begin open!
	is_intersection_occupied = false;	

	return;
}

/*
 * Called by the driver during teardown.
 * Cleanup, cleanup, everybody do their share!
 */

void stoplight_cleanup() {
	
	cv_destroy(wait_intersection);
	lock_destroy(intersection_lockdown);	
	lock_destroy(lock0);	
	lock_destroy(lock1);	
	lock_destroy(lock2);	
	lock_destroy(lock3);	
	
	return;
}

void
turnright(uint32_t direction, uint32_t index)
{
	
	// Simplest car function. Passes through quadrant: [X]	
	lock_acquire(lock_i_need(direction));	// Straight and Left will wait for this lock

	inQuadrant(direction, index);
	leaveIntersection(index);

	lock_release(lock_i_need(direction));			


	return;
}
void
gostraight(uint32_t direction, uint32_t index)
{

	// Wait for intersection to be free
	lock_acquire(intersection_lockdown);
	while(is_intersection_occupied == true){
		cv_wait(wait_intersection, intersection_lockdown);
	}
	is_intersection_occupied = true;
	
	// Car passes through quadrants: [X] -> [(X+3)%4]	
	lock_acquire(lock_i_need(direction));
	lock_acquire(lock_i_need( (direction+3) % 4 ));
	
	inQuadrant(direction, index);
	inQuadrant( (direction+3)%4 , index);
	leaveIntersection(index);
	
	lock_release(lock_i_need(direction));
	lock_release(lock_i_need( (direction+3) % 4 ));
	
	// Signal done with intersection and free resources
	is_intersection_occupied = false;
	cv_signal(wait_intersection, intersection_lockdown);
	lock_release(intersection_lockdown);

	return;
}
void
turnleft(uint32_t direction, uint32_t index)
{

	// Wait for intersection to be free
	lock_acquire(intersection_lockdown);
	while(is_intersection_occupied == true){
		cv_wait(wait_intersection, intersection_lockdown);
	}
	is_intersection_occupied = true;

	// Car passes through quadrants: [X] -> [(X+3)%4] -> [(X+2)%4]		
	lock_acquire(lock_i_need(direction));
	lock_acquire(lock_i_need( (direction+3) % 4 ));
	lock_acquire(lock_i_need( (direction+2) % 4 ));
	
	inQuadrant(direction, index);
	inQuadrant( (direction+3)%4 , index);
	inQuadrant( (direction+2)%4 , index);	
	leaveIntersection(index);
	
	lock_release(lock_i_need(direction));
	lock_release(lock_i_need( (direction+3) % 4 ));
	lock_release(lock_i_need( (direction+2) % 4 ));

	is_intersection_occupied = false;
	cv_signal(wait_intersection, intersection_lockdown);	
	lock_release(intersection_lockdown);	

	return;
}
