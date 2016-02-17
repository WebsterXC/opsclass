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
 * Driver code is in kern/tests/synchprobs.c We will
 * replace that file. This file is yours to modify as you see fit.
 *
 * You should implement your solution to the whalemating problem below.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define MAX_MATERS 64

struct semaphore *male_semaphore;
struct semaphore *fem_semaphore;
struct lock *whale_lock;

/*
 * Called by the driver during initialization.
 */

void whalemating_init() {
	// Init all synch primitives, semaphores with 0 keys
	male_semaphore = sem_create("male_sem", 0);
	fem_semaphore = sem_create("female_sem", 0);	
	whale_lock = lock_create("whalelock");	

	return;
}

/*
 * Called by the driver during teardown.
 */

void
whalemating_cleanup() {
	// On destroy, nobody should be holding the lock & both sems should be 0
	KASSERT(!lock_do_i_hold(whale_lock));
	KASSERT( (male_semaphore->sem_count == 0) && (fem_semaphore->sem_count == 0));	

	lock_destroy(whale_lock);
	sem_destroy(male_semaphore);
	sem_destroy(fem_semaphore);

	return;
}

void
male(uint32_t index)
{
	// Calling male means a new male has entered, ++1 possible
	// whales to mate with
	male_start(index);
	P(male_semaphore);
	male_end(index);

	return;
}

void
female(uint32_t index)
{
	female_start(index);
	P(fem_semaphore);
	female_end(index);	

	return;
}

void
matchmaker(uint32_t index)
{
	// Lock required so semaphores aren't altered simultaneously
	lock_acquire(whale_lock);
	matchmaker_start(index);
	V(male_semaphore); V(fem_semaphore);
	matchmaker_end(index);
	lock_release(whale_lock);	
	
	return;
}
