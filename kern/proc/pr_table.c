/*
 * Written by: William Burgin (waburgin)
 * 
 * This file implements the active-process table and
 * associated utilities. The table is implemented as 
 * single-linked, forward-traversing linked list.
 *
 */

#include <types.h>
#include <proc.h>
#include <pid.h>
#include <pr_table.h>

unsigned int num_processes;

/* Initialize process table. Called in: */
void
gpll_bootstrap(void){			// Stands for global-processes linked list
	_tail = kmalloc(sizeof(struct pnode));
	_tail->myself = NULL;
	_tail->parent = NULL;
	_tail->retcode = 32767;
	_tail->next = NULL;

	_head = kmalloc(sizeof(struct pnode));
	_head->myself = NULL;
	_head->parent = NULL;
	_head->retcode = 32766;
	_head->next = _tail;

	num_processes = 0;

	KASSERT(_tail != NULL);
	KASSERT(_head != NULL);

	return;
}

void
proc_assign(struct proc *process, struct proc *parent){
	//(void)process;
	//(void)parent;
	
	struct pnode *node;
	
	// Create pnode and fill it with some information
	node = kmalloc(sizeof(*node));
	node->retcode = 0;

	// Does this process have a parent?
	if( parent == NULL ){
		node->parent = NULL;
	}else{
		node->parent = parent;
	}
		
	// Generate a UNIQUE process ID and assign it
	pid_t attempt = pidgen();
	//while( verify_unique_pid(attempt) == false ){
	//	attempt = pidgen();
	//}
	process->pid = attempt;

	// Now fill into front of linked list (after HEAD)
	node->myself = process;		// Do not move.

	struct pnode *nptr = _head->next;
	_head->next = node;
	node->next = nptr;

	return;
}

void
proc_exited(struct proc *process){
	(void)process;

	return;
}

void
proc_nuke(struct proc *process){
	(void)process;

	return;
}

bool
verify_unique_pid(pid_t id){
	struct pnode *current = _head;
	
	// Traverse until _tail
	while( current->next != NULL ){
		if(current->myself->pid == id){
			return false;
		}
		
		current = current->next;
	}

	return true;
}

void
gpll_dump(void){

	//int counter = 0;	
	
	// Manually duplicate node
	struct pnode *current;
	current = kmalloc(sizeof(*current));
	current = _head;
	//kprintf("Head: %d\n", current->retcode);

/*	
	// Traverse until _tail
	while( current->next != NULL && counter < 55 ){
		kprintf("Recall pnode %d with PID %d\n", counter, current->myself->pid );
		//kprintf("Counter: %d\n", counter);	
		
		current = current->next;
		counter++;
	}
*/
	return;	
}


