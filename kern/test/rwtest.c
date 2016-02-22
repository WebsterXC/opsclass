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
static volatile int writercount = 0;

static void readerthread(void*, long unsigned int);
static void writerthread(void*, long unsigned int);



// Testing what heppens if they are all writers. Just making sure it works and doesn't Hang.
int rwtest(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt1 starting...\n");

	
	// Steal all of Will's stuff.
	current_char = 'A';
	writercount = 0;
	ultimate_buffer = kmalloc(sizeof(char)*64);
	for(int i = 0; i < 64; i++){
		ultimate_buffer[i] = current_char;
	}

	// Init primitives for testing & be sure they exist
	exitsem = sem_create("exitsem", 0);
	test_rwlk = rwlock_create("test_read_write_lk");
	printlock = lock_create("kprintf_lk");
	
	if( printlock == NULL || test_rwlk == NULL || exitsem == NULL ){
		panic("rwtest1: failed to create synch primitives");
	}

	
	int err;
	// Create threads and fork them
		for(int j = 0; j < N_THREADS; j++){
			if(true){
				err = thread_fork("rwtest1", NULL, writerthread, NULL, j);
			}else{	
				err = thread_fork("rwtest1", NULL, readerthread, NULL, j);
			}
			random_yielder(2);
			if(err){
				panic("rwtest1 thread fork failure.");
			}
		}

	for(int k = 0; k < N_THREADS; k++){
		P(exitsem);
	}

	// Clean up. 

	lock_destroy(printlock);
	rwlock_destroy(test_rwlk);
	sem_destroy(exitsem);

	success(SUCCESS, SECRET, "rwt1");

	return 0;

}
	

// Jacking Will's Test 3 and making it every 3 multiple.

int rwtest2(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt2 starting...\n");

	
	// Initialize all that good stuff.
	current_char = 'A';
	writercount = 0;
	ultimate_buffer = kmalloc(sizeof(char)*64);
	for(int i = 0; i < 64; i++){
		ultimate_buffer[i] = current_char;
	}

	// Init primitives for testing & be sure they exist
	exitsem = sem_create("exitsem", 0);
	test_rwlk = rwlock_create("test_read_write_lk");
	printlock = lock_create("kprintf_lk");
	
	if( printlock == NULL || test_rwlk == NULL || exitsem == NULL ){
		panic("rwtest2: failed to create synch primitives");
	}

	
	int err;
	// Create threads and fork them
		for(int j = 0; j < N_THREADS; j++){
			if((j%3) == 0){ 
				err = thread_fork("rwtest1", NULL, writerthread, NULL, j);
			}else{	
				err = thread_fork("rwtest1", NULL, readerthread, NULL, j);
			}
			random_yielder(2);
			if(err){
				panic("rwtest1 thread fork failure.");
			}
		}

	for(int k = 0; k < N_THREADS; k++){
		P(exitsem);
	}

	// Deicide!
	lock_destroy(printlock);
	rwlock_destroy(test_rwlk);
	sem_destroy(exitsem);

	success(SUCCESS, SECRET, "rwt2");

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

//static volatile int writercount = 0; ----- Added it to the very top. Hope this doens't break anything.
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
		success(FAIL, SECRET, "Test Fail. Reader Fault."); 
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
		success(FAIL, SECRET, "Test Fail. Writer Fault.");
	}

	kprintf("Writer writing: %c\n", current_char);
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
	writercount = 0;
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

	// Cleanup because we're good programmers!

	lock_destroy(printlock);
	rwlock_destroy(test_rwlk);
	sem_destroy(exitsem);

	success(SUCCESS, SECRET, "rwt3");

	return 0;
}

/*
 * R/W Lock Test 4: William Burgin
 * Forks N_THREADS/2 threads, all of which are writers except for one reader.
 * This test is meant to demonstrate that a reader is still allowed into
 * a resource despite the writers having priority and constantly pounding
 * the buffer. This test uses a 64 slot buffer.
 */

int rwtest4(int nargs, char **args) {
	(void)nargs;
	(void)args;

	// Initialize resources
	current_char = 'A';
	writercount = 0;
	ultimate_buffer = kmalloc(sizeof(char)*64);
	for(int i = 0; i < 64; i++){
		ultimate_buffer[i] = current_char;
	}

	// Init primitives for testing & be sure they exist
	exitsem = sem_create("exitsem", 0);
	test_rwlk = rwlock_create("test_read_write_lk");
	printlock = lock_create("kprintf_lk");
	
	if( printlock == NULL || test_rwlk == NULL || exitsem == NULL ){
		panic("rwtest4: failed to create synch primitives");
	}

	int err;
	kprintf("Begin.");
	for(int j = 0; j < N_THREADS/2; j++){
		if( j == 20 || j == 30 ){	// Threads 20 & 30 are readers
			err = thread_fork("rwtest4", NULL, readerthread, NULL, j);
		}else{
			err = thread_fork("rwtest4", NULL, writerthread, NULL, j);
		}
		
		if(err){
			panic("rwtest4 thread fork failure");
		}

	}
	
	for(int k = 0; k < N_THREADS/2; k++){
		P(exitsem);
	}

		
	lock_destroy(printlock);
	rwlock_destroy(test_rwlk);
	sem_destroy(exitsem);

	success(SUCCESS, SECRET, "rwt4");

	return 0;
}

/*
 * R/W Lock Test 5: William Burgin
 * Some pretty aweful punishment of the buffer and R/W locks. Forks
 * N_THREADS*4 threads, odd numbers being writers and evens being readers.
 * Same stuff as before: 64 slot buffer. Despite the number of iterations
 * being more than the ASCII table, we can be sure that the failure is
 * thrown properly because the threads do direct comparison of the 
 * expected character to the read character.
 */

int rwtest5(int nargs, char **args) {
	(void)nargs;
	(void)args;

	
	// Initialize resources
	current_char = 'A';
	writercount = 0;
	ultimate_buffer = kmalloc(sizeof(char)*64);
	for(int i = 0; i < 64; i++){
		ultimate_buffer[i] = current_char;
	}

	// Init primitives for testing & be sure they exist
	exitsem = sem_create("exitsem", 0);
	test_rwlk = rwlock_create("test_read_write_lk");
	printlock = lock_create("kprintf_lk");
	
	if( printlock == NULL || test_rwlk == NULL || exitsem == NULL ){
		panic("rwtest5: failed to create synch primitives");
	}

	int err;
	kprintf("Begin.");
	for(int j = 0; j < N_THREADS*4; j++){
		if( (j%2) == 0){
			err = thread_fork("rwtest5", NULL, readerthread, NULL, j);
		}else{
			err = thread_fork("rwtest5", NULL, writerthread, NULL, j);
		}

		if(err){
			panic("rwtest5 thread fork failure");
		}	
		
	}

	for(int k = 0; k < N_THREADS*4; k++){
		P(exitsem);		
	}

		
	lock_destroy(printlock);
	rwlock_destroy(test_rwlk);
	sem_destroy(exitsem);

	success(SUCCESS, SECRET, "rwt5");

	return 0;
}
