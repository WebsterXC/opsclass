/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
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
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
	struct semaphore *sem;

	sem = kmalloc(sizeof(*sem));
	if (sem == NULL) {
		return NULL;
	}

	sem->sem_name = kstrdup(name);
	if (sem->sem_name == NULL) {
		kfree(sem);
		return NULL;
	}

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
	sem->sem_count = initial_count;

	return sem;
}

void
sem_destroy(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
	kfree(sem->sem_name);
	kfree(sem);
}

void
P(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the P without blocking.
	 */
	KASSERT(curthread->t_in_interrupt == false);

	/* Use the semaphore spinlock to protect the wchan as well. */
	spinlock_acquire(&sem->sem_lock);
	while (sem->sem_count == 0) {
		/*
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_sleep(sem->sem_wchan, &sem->sem_lock);
	}
	KASSERT(sem->sem_count > 0);
	sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

void
V(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

	sem->sem_count++;
	KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
	struct lock *lock;

	lock = kmalloc(sizeof(*lock));
	if (lock == NULL) {
		return NULL;
	}

	lock->lk_name = kstrdup(name);
	if (lock->lk_name == NULL) {
		kfree(lock);
		return NULL;
	}
	
	lock->lk_wchan = wchan_create(lock->lk_name);
	if (lock->lk_wchan == NULL) {
		kfree(lock);
		return NULL;
	}
	
	spinlock_init(&lock->lk_spinlock);
	
	// A lock is similar to a semaphore with only one slot
	lock->lock_count = 1;
	
	// Initialize holder to have no holder
	lock->lk_holder = NULL;	

	return lock;
}

void
lock_destroy(struct lock *lock)
{
	KASSERT(lock != NULL);

	// Free all memory contained in lock struct
	spinlock_cleanup(&lock->lk_spinlock);
	wchan_destroy(lock->lk_wchan);
	
	// When lock is destroyed, no thread should be holding it
	KASSERT(lock->lk_holder == NULL);
	kfree(lock->lk_name);
	kfree(lock);
}

void
lock_acquire(struct lock *lock)
{	
	KASSERT(lock != NULL);	 

	spinlock_acquire(&lock->lk_spinlock);	

	//spinlock_acquire(&lock->lk_spinlock);
	while(lock->lock_count == 0){
		// While there are no slots for the lock, wait on		
		// held wait channel
		//spinlock_release(&lock->lk_spinlock);
		wchan_sleep(lock->lk_wchan, &lock->lk_spinlock);
	}
	
	KASSERT(lock->lock_count == 1);

	// Assign holder
	lock->lk_holder = curthread;	

	// Decrease lock count to 0 to hold it
	lock->lock_count--;
	spinlock_release(&lock->lk_spinlock);	

}

void
lock_release(struct lock *lock)
{
	// Make sure lock isn't NULL
	KASSERT(lock != NULL);
	
	// Only the thread holding the lock may do this
	KASSERT(lock_do_i_hold(lock));	

	// Acquire Spinlock
	spinlock_acquire(&lock->lk_spinlock);		
	
	// Increase counter, notify done using lock via wakeup on wait channel
	lock->lock_count++;
	KASSERT(lock->lock_count > 0);	// But first make sure it was a success!
	
	// Remove holder
	lock->lk_holder = NULL;

	wchan_wakeone(lock->lk_wchan, &lock->lk_spinlock);
	
	// Release spinlock
	spinlock_release(&lock->lk_spinlock);

}

bool
lock_do_i_hold(struct lock *lock)
{		
	
	KASSERT(lock != NULL);	

	// Verify thread holder in struct is the current thread
	if((lock->lk_holder) != curthread){
		return false;
	}		

	return true;
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{
	struct cv *cv;

	cv = kmalloc(sizeof(*cv));
	if (cv == NULL) {
		return NULL;
	}

	cv->cv_name = kstrdup(name);
	if (cv->cv_name==NULL) {
		kfree(cv);
		return NULL;
	}

	spinlock_init(&cv->cv_spinlock);	
	if (&cv->cv_spinlock==NULL) {
		kfree(cv);
		return NULL;
	}

	return cv;
}

void
cv_destroy(struct cv *cv)
{
	KASSERT(cv != NULL);

	// Ensure nobody holds the spinlock before imminent destruction	
	KASSERT(!spinlock_do_i_hold(&cv->cv_spinlock));
	
	kfree(cv->cv_name);
	spinlock_cleanup(&cv->cv_spinlock);
	kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	// Write this
	KASSERT(cv != NULL);
		
	// Lock ownership required 	
	spinlock_acquire(&cv->cv_spinlock);
	lock_release(lock);
	wchan_sleep(lock->lk_wchan, &cv->cv_spinlock);
	spinlock_release(&cv->cv_spinlock);
	lock_acquire(lock);

}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	
	KASSERT(cv != NULL);
	KASSERT(lock_do_i_hold(lock));	
	spinlock_acquire(&cv->cv_spinlock);
	wchan_wakeone(lock->lk_wchan, &cv->cv_spinlock);	
	spinlock_release(&cv->cv_spinlock);

}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{

	KASSERT(cv != NULL);
	KASSERT(lock_do_i_hold(lock));
	spinlock_acquire(&cv->cv_spinlock);
	wchan_wakeall(lock->lk_wchan, &cv->cv_spinlock);
	spinlock_release(&cv->cv_spinlock);	

}
///////////////////////////////////////////////////////////////////
//
// Read - Write Locks (2/18 8:30AM -> Untested)

struct rwlock *
rwlock_create(const char *name){
	
	struct rwlock *rwlock;
	rwlock = kmalloc(sizeof(*rwlock));

	rwlock->rw_name = kstrdup(name);
	if (rwlock->rw_name == NULL) {
		kfree(rwlock);
		return NULL;
	}

	rwlock->rw_conditional = cv_create(rwlock->rw_name);
	if (&rwlock->rw_conditional == NULL) {
		kfree(rwlock);
		return NULL;
	}

	rwlock->rw_lock = lock_create(rwlock->rw_name);	
	if (&rwlock->rw_lock == NULL) {
		kfree(rwlock);
		return NULL;
	}	
	
	// Set current number of readers to 0; no writers waiting
	rwlock->rw_num_readers = 0;
	rwlock->is_writer_waiting = false;

	return rwlock;	
}

void
rwlock_destroy(struct rwlock *rwlock){
	//KASSERTS	
	
	KASSERT(rwlock != NULL);
	KASSERT(!lock_do_i_hold(rwlock->rw_lock));
	
	kfree(rwlock->rw_name);
	lock_destroy(rwlock->rw_lock);
	cv_destroy(rwlock->rw_conditional);	
	kfree(rwlock);
	
	return;
}

void
rwlock_acquire_read(struct rwlock *rwlock){
	
	KASSERT(rwlock != NULL);
	
	lock_acquire(rwlock->rw_lock); 
	
	// If writers are waiting, wait. Waits until all readers hit this if-statement and num_readers is 0
	if(rwlock->is_writer_waiting){
		//rwlock->rw_num_readers--;	//Safe because currently holding lock
		cv_wait(rwlock->rw_conditional, rwlock->rw_lock);
	}	

	rwlock->rw_num_readers++;
	lock_release(rwlock->rw_lock);

	//(void)rwlock;
	return;	
}

void
rwlock_release_read(struct rwlock *rwlock){
	
	KASSERT(rwlock->rw_num_readers > 0);
	
	lock_acquire(rwlock->rw_lock);
	rwlock->rw_num_readers--;
	lock_release(rwlock->rw_lock);
	

	//(void)rwlock;
	return;
}

void
rwlock_acquire_write(struct rwlock *rwlock){
	
	KASSERT(rwlock != NULL);
	
	// Set flag to declare waiting. Readers will cv_wait if this flag is true
	lock_acquire(rwlock->rw_lock);
	rwlock->is_writer_waiting = true;
	lock_release(rwlock->rw_lock);
	
	// Loop until all readers are on cv_wait. Then proceed.
	while(rwlock->rw_num_readers > 0){
		//Busy wait			
	}
	KASSERT(rwlock->rw_num_readers == 0);	
	
	// Writer now exclusively owns access, readers are all on cv_wait

	//(void)rwlock;
	return;
}

void
rwlock_release_write(struct rwlock *rwlock){
	
	KASSERT(rwlock != NULL);

	// Swap flag back to false when done writing
	lock_acquire(rwlock->rw_lock);
	rwlock->is_writer_waiting = false;
	
	// Let readers on cv_wait know they can resume reading the resource
	cv_broadcast(rwlock->rw_conditional, rwlock->rw_lock);
	
	lock_release(rwlock->rw_lock);	


	//(void)rwlock;
	return;
}
