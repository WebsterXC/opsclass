/*
 * Associated header file for the process table.
 * Written by: William Burgin (waburgin)
 */

#include <types.h>

/* Define nodes for linked list */
struct pnode{
	/* Process this node represents. */
	struct proc *myself;

	/* Parent process. Proc struct contains has_parent flag. */
	struct proc *parent;
	

	int retcode;	// Exit code here. HEAD contains 32766, TAIL contains 32767

	/* Pointer to next node in table. */
	/* TAIL contains a NULL pointer */
	struct pnode *next;
	
};

/* Linked List Frame */
struct pnode *_tail;
struct pnode *_head;

/* Internal Methods */
bool verify_unique_pid(pid_t);

/* External Methods */
void gpll_bootstrap(void);
void proc_assign(struct proc *process, struct proc *parent);
void proc_exited(struct proc *process);
void proc_nuke(struct proc *process);

void gpll_dump(void);
