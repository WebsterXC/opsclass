/*
 * Associated header file for the process table.
 * Written by: William Burgin (waburgin)
 */

#include <types.h>

/* Define nodes for linked list */
struct pnode{
	/* Process this node represents. */
	struct proc *myself;	
	pid_t pid;

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
void proc_assign(struct proc *process);
void proc_exited(struct proc *process);
void proc_nuke(struct proc *process);

struct proc * proc_getptr(pid_t id);
pid_t proc_getpid(struct proc *process);
struct pnode * proc_get_pnode(struct proc *process);


void gpll_dump(void);
