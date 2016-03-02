/* System fork() file.
 * Written by: William Burgin (waburgin)
 *
 */

#include <types.h>
#include <current.h>
#include <lib.h>
#include <thread.h>
#include <synch.h>
#include <mips/trapframe.h>
#include <addrspace.h>
#include <proc.h>
#include <syscall.h>
#include <filetable.h>

/* Wrapper for the child process.
 * Data1: Struct holding a trapframe and addrspace pointer
 * Data2: Child's new PID
 */
void
child(void *fk_img, long unsigned int data2){
	(void)data2;
	//cv_wait(fk_img->cv);			// Wait for parent to finish setup

	// Copy file table
	spinlock_acquire(&curproc->p_lock);
	filetable_copy( ((struct forkimage *)fk_img)->filetab, &curproc->p_filetable);
	spinlock_release(&curproc->p_lock);

	// Load and activate the address space	`	
	proc_setas( ((struct forkimage *)fk_img)->addr );	
	as_activate();	
	
	// Alter trapframe so child returns 0
	// Switch to usermode
	enter_forked_process( ((struct forkimage *)fk_img)->trap );

}

int
sys_fork(struct trapframe *frame, int32_t *childpid){

	/* Allocate space for a address space & trapframe copy */
	struct filetable *file_copy;	//	Size reference
	struct proc *fakeproc;
	struct forkimage *fk_img;
	
	int result;		
	
	// Fork image contains trapframe and address space pointers, and a pointer to current filetable
	fk_img = kmalloc(sizeof(*fk_img));
	fk_img->addr = kmalloc(sizeof(*fakeproc));
	as_copy(curproc->p_addrspace, &fk_img->addr);

	fk_img->trap = kmalloc(sizeof(*frame));			// Allocate and copy parent's trapframe
	memcpy(fk_img->trap, frame, sizeof(*frame));
	
	fk_img->filetab = kmalloc(sizeof(*file_copy));
	memcpy(fk_img->filetab, curproc->p_filetable, sizeof(*file_copy));

	// Copy file table??
	(void)file_copy;

	// TODO: Change 0 to child's new pid
	result = thread_fork(curproc->p_name, NULL, child, fk_img, 0);  	 

	//cv_signal(fk_img->cv);
	
	if(result){
		//Error?
	}

	*childpid = 1717;
	return 1717;
}

