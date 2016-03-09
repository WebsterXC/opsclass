/*
 * Written by: William Burgin (waburgin)
 * 
 * This file implements the active-process table and
 * associated utilities. The table is implemented as 
 * single-linked, forward-traversing linked list.
 *
 */

#include <kern/errno.h>
#include <types.h>
#include <proc.h>
#include <pid.h>
#include <pr_table.h>

unsigned int num_processes;

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
	
	// Create pnode and fill it with some information
	node = kmalloc(sizeof(*node));
	node->retcode = 0;
	if( node == NULL ){
		kfree(node);
		return;
	}
	
	// Generate a UNIQUE process ID and assign it
	pid_t attempt = pidgen();
	while( verify_unique_pid(attempt) == false ){
		attempt = pidgen();
	}
	node->pid = attempt;

	/* Other assignments go here */
	process->isactive = true;	

	/* Make the node aware of it's own process */
	node->myself = process;		// Do not move.

	/* Now fill into front of linked list (after _head of course) */
	struct pnode *nptr;
	nptr = kmalloc(sizeof(*nptr));
	nptr = _head->next;
	_head->next = node;
	node->next = nptr;

	//num_processes++;

	return;
}

/* Creates an exited, but accessible process in the process list. Child processes call this
 * function to indicate they have exited, but remain searchable in the global linked list
 * for the parent to find.

 * Processes that call this function are NOT removed from memory
 */

void
proc_exited(struct proc *process){

	struct pnode *node;
	
	node = proc_get_pnode(process);
	if( node == NULL ){
		return;
	}
	
	process->isactive = false;

	return;
}

/* Destroys a process and associated pnode completely. This is called by parents processes
 * who exit as well as by children after their exit code has been collected or respective
 * parent has exited.
 */

void
proc_nuke(struct proc *process){
	
	// Find the pnode the process is in
	struct pnode *current;
	struct pnode *last;
	
	current = kmalloc(sizeof(*current));
	last = kmalloc(sizeof(*last));
	
	current = _head->next;	

	while( current->next != NULL ){
		if( current->next->myself == process ){		// Compare pointers
			last = current;
			current = current->next;
			break;
		}		

		current = current->next;
	}

	// Reassign pointers
	last->next = current->next;

	// Free memory at process' pnode
	current->myself = NULL;
	current->next = NULL;
	kfree(current);
	
	//num_processes--;
	
	return;
}

/* Returns a pointer to a process with supplied PID */
struct proc *
proc_getptr(pid_t id){
	struct pnode *current;
	current = kmalloc(sizeof(*current));
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
	current = kmalloc(sizeof(*current));
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
	current = kmalloc(sizeof(*current));
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

/* Compares a PID to the enitre process list and returns false 
 * if the PID is not unique. 
 */
bool
verify_unique_pid(pid_t id){
	struct pnode *current;
	current = kmalloc(sizeof(*current));
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

void
gpll_dump(void){

	int counter = 0;	
	
	// Manually duplicate node
	struct pnode *current;
	current = kmalloc(sizeof(*current));
	current = _head->next;
	kprintf("Recall pnode _head with PID %d\n", _head->pid);

	// Traverse until _tail
	while( current->next != NULL ){
		kprintf("Recall pnode %d with PID %d\n", counter, current->pid );
		//kprintf("Counter: %d\n", counter);	
		
		current = current->next;
		counter++;
	}

	kprintf("Recall pnode _tail with PID %d\n", _tail->pid);
		

	return;	
}

