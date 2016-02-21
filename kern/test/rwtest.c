/*
 * All the contents of this file are overwritten during automated
 * testing. Please consider this before changing anything in this file.
 */

#include <types.h>
#include <lib.h>
#include <clock.h>
#include <thread.h>
#include <synch.h>
#include <test.h>
#include <kern/secret.h>
#include <spinlock.h>

#define N_THREADS 128

/*
 * Use these stubs to test your reader-writer locks.
 */

static struct semaphore *exitsem = NULL;

static struct rwlock *test_rwlk = NULL;
static struct lock *printlock = NULL;

static volatile char current_char;
static volatile char *ultimate_buffer = NULL;
static volatile char cur_num_readers;

static void readerthread(void*, long unsigned int);
static void writerthread(void*, long unsigned int);

int rwtest(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt1 unimplemented\n");
	success(FAIL, SECRET, "rwt1");

	return 0;
}

int rwtest2(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt2 unimplemented\n");
	success(FAIL, SECRET, "rwt2");

	return 0;
}

/*
 * R/W Lock Test 3: William Burgin
 * Forks N_THREADS threads, with every 4th being a write thread.
 * After scrambling, threads try to read & write from a 64 char buffer.
 * This is PSEUDORANDOM thread order (vs sequential).
 * It's 
 */

/*
 * Reader thread expects to see a buffer full of the current_char and then prints
 * what's in the buffer. If they don't equal, the test will fail.
 */
static volatile int writercount = 0;
static void readerthread(void *unused, long unsigned int id){		
	random_yielder(4);	// Scrambled eggs & ham

	lock_acquire(printlock);
	rwlock_acquire_read(test_rwlk);	
	cur_num_readers++;
	
	// Calculate expected char. A counter keeps track of the # of writers
	// and does the same calc as the writerthread here.
	char testchar = 'A' + writercount;	

	// Dump ultimate_buffer to screen
	kprintf("Reader %d expected: %c | ", (int)id, testchar);
	for(int i = 0; i < 64; i++){
		kprintf("%c",ultimate_buffer[i]);
	}
	kprintf("\n");

	// Ensure what was read matches up with the predicted output.
	if(testchar != ultimate_buffer[0]){
		success(FAIL, SECRET, "rw3"); 
	}

	cur_num_readers--;

	rwlock_release_read(test_rwlk);
	lock_release(printlock);

	(void)unused;
	(void)id;
	V(exitsem);
	return;
}

/*
 * Writerthread increased the value at current_char and then writes that value to
 * the entire length of ultimate_buffer.
 */
static void writerthread(void *unused, long unsigned int id){
	random_yielder(4);	//Scramble

	lock_acquire(printlock);
	rwlock_acquire_write(test_rwlk);
	
	current_char++;
	writercount++;

	// Ensure writer is by itself
	if(cur_num_readers > 0){
		success(FAIL, SECRET, "rw3");
	}

	kprintf("Writer writing %c.\n", current_char);
	for(int i = 0; i < 64; i++){
		ultimate_buffer[i] = current_char;
	}

	rwlock_release_write(test_rwlk);
	lock_release(printlock);

	(void)unused;
	(void)id;
	V(exitsem);
	return;
}

int rwtest3(int nargs, char **args) {
	(void)nargs;
	(void)args;
	
	// Initialize resources and other fun stuff
	// See above for current_char description
	current_char = 'A';
	ultimate_buffer = kmalloc(sizeof(char)*64);
	for(int i = 0; i < 64; i++){
		ultimate_buffer[i] = current_char;
	}
	
	// Check if the count is 0 at the end of test. If not, fail!
	exitsem = sem_create("exitsem", 0);
	test_rwlk = rwlock_create("test_read_write_lk");
	printlock = lock_create("kprintf_lk");	

	// Panic if read/write lock doesn't exist
	if( printlock == NULL || test_rwlk == NULL || exitsem == NULL ){
		panic("rwtest3: failed to create synch primitives");
	}

	int err; int rotations = 0;
	//while(rotations < 1){
	// Create threads and fork them
		kprintf("Begin.");
		for(int j = 0; j < N_THREADS; j++){
			if( (j%4) == 0){
				//kprintf("Writer %d started.\n", j);
				err = thread_fork("rwtest3", NULL, writerthread, NULL, j);
			}else{	
				//kprintf("Reader %d started.\n", j);
				err = thread_fork("rwtest3", NULL, readerthread, NULL, j);
			}
			random_yielder(2);
			if(err){
				panic("rwtest3 thread fork failure.");
			}
		//random_yielder(2);
		}
		rotations++;
	//}

	for(int k = 0; k < N_THREADS; k++){
		P(exitsem);
	}
	//while(readers_running->sem_count > 0){ }	
	//kprintf_n("rwt3 unimplemented\n");
	success(SUCCESS, SECRET, "rwt3");

	return 0;
}

int rwtest4(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt4 unimplemented\n");
	success(FAIL, SECRET, "rwt4");

	return 0;
}

int rwtest5(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt5 unimplemented\n");
	success(FAIL, SECRET, "rwt5");

	return 0;
}
