/*
 * Copyright (c) 2013
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
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */

#include <types.h>
#include <limits.h>
#include <lib.h>
#include <kern/errno.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <filetable.h>

/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;
volatile unsigned int num_processes;

pid_t
pidgen(void){
	// Return a random pid between 2 and 32767
	return (pid_t)(random() % (__PID_MAX + 1 - __PID_MIN) + __PID_MIN);	
}


/* Initialize process table. Called in: main.c */
void
gpll_bootstrap(void){			// Stands for global-processes linked list
	_tail = kmalloc(sizeof(struct pnode));
	_tail->myself = NULL;
	_tail->pid = -2;
	_tail->retcode = 32767;
	_tail->next = NULL;

	_head = kmalloc(sizeof(struct pnode));
	_head->myself = NULL;
	_head->pid = -1;
	_head->retcode = 32766;
	_head->next = _tail;

	gpll_lock = lock_create("GPLL Lock");
	gpll_cv = cv_create("GPLL CV");

	num_processes = 0;

	KASSERT(_tail != NULL);
	KASSERT(_head != NULL);

	return;
}

/* Assigns a process to the process list. This is the function that physically adds new nodes
 * to the global linked-list that keeps track of all processes. Each process is assigned it's
 * own, unique PID. This function also assigns the process as "active", meaning it can/is running
 * in the system currently. On the contrary, processes that are not active, but exist in the linked
 * list are children who have exited, but are still waiting on their parent to collect the exit
 * code.
 */

void
proc_assign(struct proc *process){

	struct pnode *node;
	
	//kprintf("Assign.\n");
	// Create pnode and fill it with some information
	node = kmalloc(sizeof(*node));
	node->retcode = 0;
	if( node == NULL ){
		//kprintf("Node.\n");
		kfree(node);
		return;
	}

	node->exitsem = sem_create("exitsem", 0);
	
	// Generate a UNIQUE process ID and assign it
	pid_t attempt = pidgen();
	while( verify_unique_pid(attempt) == false ){
		attempt = pidgen();
	}
	node->pid = attempt;
	//kprintf("Created: %d\n", attempt);

	/* Make the node aware of it's own process */
	node->myself = process;		// Do not move.

	/* Now fill into front of linked list (after _head of course) */
	struct pnode *nptr;
	nptr = _head->next;
	_head->next = node;
	node->next = nptr;

	num_processes++;
	//kprintf("Num: %d\n", num_processes);

	return;
}


/* Destroys a process and associated pnode completely. This is called by parents processes
 * who exit as well as by children after their exit code has been collected or respective
 * parent has exited.
 */

int
proc_nuke(struct proc *process){
	// Find the pnode the process is in
	struct pnode *current;
	struct pnode *last;
	
	//kprintf("Nuking: ");
	
	//current = kmalloc(sizeof(*current));
	//last = kmalloc(sizeof(*last));
	
	current = _head;	

	while( current->next != NULL ){
		if( (current->next)->myself == process ){		// Compare pointers
			last = current;
			current = current->next;
			break;
		}		

		current = current->next;
	}
	//kprintf("Del: %d\n", current->pid);

	if( current->pid == -1 || current->pid == -2){
		kprintf("Err.\n");
		return ENOMEM;
	}	

	
	// Reassign pointers
	last->next = current->next;

	sem_destroy(current->exitsem);
	// Free memory at process' pnode
	
	kfree(current);
	num_processes--;
	//kprintf("Num: %d\n", num_processes);

	return 0;
}

/* Returns a pointer to a process with supplied PID */
struct proc *
proc_getptr(pid_t id){
	struct pnode *current;
	//current = kmalloc(sizeof(*current));
	current = _head->next;

	while( current->next != NULL ){
		if(current->pid == id){
			return current->myself;
		}

		current = current->next;
	}

	return NULL;
}

/* Returns PID of a process with supplied process ptr */
pid_t
proc_getpid(struct proc *process){
	struct pnode *current;
	//current = kmalloc(sizeof(*current));
	current = _head->next;

	while( current->next != NULL ){
		if(current->myself == process){
			return current->pid;
		}
		current = current->next;
	}

	return -1;
}

/* Gets a pointer to the pnode that contains the given process */
struct pnode *
proc_get_pnode(struct proc *process){
	struct pnode *current;
	//current = kmalloc(sizeof(*current));
	current = _head->next;

	while( current->next != NULL ){
		if(current->myself == process){
			return current;
		}
		current = current->next;
	}

	return NULL;
}
/* Returns the current number of user processes */
unsigned int
proc_rollcall(void){
	return num_processes;
}

/*
struct pnode *
proc_get_pnodelst(struct proc *process){
	struct pnode *current;
	current = _head;
	
	while( current->next != NULL){
		int id = proc_getpid(current->pid);
		if( id		

	}
}
*/

/* Compares a PID to the enitre process list and returns false 
 * if the PID is not unique. 
 */
bool
verify_unique_pid(pid_t id){
	struct pnode *current;
	//current = kmalloc(sizeof(*current));
	current = _head;	

	// Traverse until _tail
	while( current->next != NULL ){
		if(current->pid == id){
			return false;
		}
		
		current = current->next;
	}

	return true;
}




/*
 * Create a proc structure.
 */

static
struct proc *
proc_create(const char *name)
{
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}

	proc->p_numthreads = 0;
	spinlock_init(&proc->p_lock);
	
	proc->forksem = sem_create("forksem", 0);
	if (proc->forksem == NULL){
		kfree(proc);
		return NULL;
	}

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;
	proc->p_filetable = NULL;
	
	proc->parent = NULL;

	return proc;
}

/*
 * Destroy a proc structure.
 *
 * Note: nothing currently calls this. Your wait/exit code will
 * probably want to do so.
 */
void
proc_destroy(struct proc *proc)
{
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */
	
	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}
	if (proc->p_filetable) {
		filetable_destroy(proc->p_filetable);
		proc->p_filetable = NULL;
	}
	
	int result;
	// Remove from process list
	result = proc_nuke(proc);
	if(result){
		return;
	}

	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */
		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}


	KASSERT(proc->p_numthreads == 0);
	spinlock_cleanup(&proc->p_lock);

	sem_destroy(proc->forksem);
	
	kfree(proc->p_name);
	kfree(proc);
}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
	kproc = proc_create("[kernel]");
	if (kproc == NULL) {
		panic("proc_create for kproc failed\n");
	}
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 *
 * It will be given no filetable. The filetable will be initialized in
 * runprogram().
 */
struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *newproc;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}

	/* VM fields */

	newproc->p_addrspace = NULL;

	/* VFS fields */

	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	// Add to process list
	proc_assign(newproc);

	return newproc;
}

/*
 * Clone the current process.
 *
 * The new thread is given a copy of the caller's file handles if RET
 * is not null. (If RET is null, what we're creating is a kernel-only
 * thread and it doesn't need an address space or file handles.)
 * However, the new thread always inherits its current working
 * directory from the caller. The new thread is given no address space
 * (the caller decides that).
 */
int
proc_fork(struct proc **ret)
{
	struct proc *proc;
	struct filetable *tbl;
	int result;

	proc = proc_create(curproc->p_name);
	if (proc == NULL) {
		return ENOMEM;
	}

	/* VM fields */
	/* do not clone address space -- let caller decide on that */ 
		

	/* VFS fields */
	tbl = curproc->p_filetable;
	if (tbl != NULL) {
		result = filetable_copy(tbl, &proc->p_filetable);
		if (result) {
			as_destroy(proc->p_addrspace);
			proc->p_addrspace = NULL;
			proc_destroy(proc);
			return result;
		}
	}

	spinlock_acquire(&curproc->p_lock);
	/* we don't need to lock proc->p_lock as we have the only reference */
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		proc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	// Assign parent to new forked process
	proc->parent = curproc;

	*ret = proc;
	
	// Add to process list
	proc_assign(*ret);

	struct pnode *node;
	struct pnode *parnode;
	parnode = proc_get_pnode(curproc);
	node = proc_get_pnode(proc);
	if(node != NULL && parnode != NULL){
		node->pid_parent = parnode->pid;
		kprintf("PPID: %d\n", parnode->pid);
	}	


	return 0;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	proc->p_numthreads++;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = proc;
	splx(spl);

	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	int spl;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	KASSERT(proc->p_numthreads > 0);
	proc->p_numthreads--;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = NULL;
	splx(spl);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}
