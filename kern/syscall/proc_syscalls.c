/* System fork() file.
 * Written by: William Burgin (waburgin)
 *
 */


#include <kern/errno.h>
#include <kern/unistd.h>

#include <types.h>
#include <current.h>
#include <lib.h>
#include <thread.h>
#include <synch.h>
#include <kern/wait.h>
#include <mips/trapframe.h>
#include <addrspace.h>
#include <proc.h>
#include <pr_table.h>
#include <syscall.h>
#include <filetable.h>
#include <openfile.h>
#include <vfs.h>
#include <vm.h>
#include <copyinout.h>
#include <kern/fcntl.h>

/* Wrapper for the child process.
 * Data1: Struct holding a trapframe and addrspace pointer
 * Data2: Child's new PID
 */
void
child(void *fk_img, long unsigned int data2){
	(void)data2;

	// Copy file table
	filetable_copy( ((struct forkimage *)fk_img)->filetab, &curproc->p_filetable);

	// Load and activate the address space	`	
	proc_setas( ((struct forkimage *)fk_img)->addr );	
	as_activate();	
	
	// Alter trapframe so child returns 0
	// Switch to usermode
	// See: arch/mips/syscall/syscall.c
	enter_forked_process( ((struct forkimage *)fk_img)->trap );

}

int
sys_fork(struct trapframe *frame, int32_t *childpid){

	/* Allocate space for a address space & trapframe copy */
	struct filetable *file_copy;
	struct proc *childproc;
	struct forkimage *fk_img;
	
	int result;		
	
	/* Generate a "fork image" to pass to the child handle function.
	 * A fork image consists of heap copies of the current process' address
	 * space, trapframe, and filetable. 
	 */

	fk_img = kmalloc(sizeof(*fk_img));
	fk_img->addr = kmalloc(sizeof(*childproc));
	as_copy(curproc->p_addrspace, &fk_img->addr);

	fk_img->trap = kmalloc(sizeof(*frame));			// Allocate and copy parent's trapframe
	memcpy(fk_img->trap, frame, sizeof(*frame));
	
	fk_img->filetab = kmalloc(sizeof(*file_copy));
	filetable_copy(curproc->p_filetable, &fk_img->filetab);

	/* Call proc_fork, which copies an almost-exact copy of the current
	 * process to the childproc. It leaves out copying the address space,
	 * so we copy the contents of that to the child as well.
	 */

	/* Proc_assign is called inside of proc_fork, which adds the child process to the
	 * process list. We must find this node and get the newly assigned child PID. This
	 * also serves in verifying the child exists on the process list.
	 */
 
	result = proc_fork(&childproc);
	if(result){
		return ENOMEM;
	}
	as_copy(curproc->p_addrspace, &childproc->p_addrspace);
	
	// Fork the process through the child() method above, associated with childproc.
	result = thread_fork(curproc->p_name, childproc, child, fk_img, 0);  	 
	
	if(result == ENOMEM){
		return ENOMEM;
	}else if(result){
		return -1;
	}

	// Return with child's PID
	*childpid = proc_getpid(childproc);
	
	return 0;
}

int
sys_waitpid(pid_t pid, int *status, int options, int *childpid){
	//(void)pid;
	(void)status;
	(void)options;

	/* Check all of the args for errors */


	/* Get waiter process pnode */
	struct proc *waiterprocess;
	struct pnode *node;
	waiterprocess = proc_getptr(pid);

	/* Determine if waiter process exists */
	if( waiterprocess == NULL ){
		return -1;	
	}

	node = proc_get_pnode(waiterprocess);
	KASSERT( node->myself == waiterprocess );

	/* Determine if waiter process has finished */
	if( waiterprocess->isactive == true ){
		// Waiter process is still running, wait for it to finish
		lock_acquire(waiterprocess->p_cv_lock);
		cv_wait( waiterprocess->p_cv, waiterprocess->p_cv_lock );
		*status = node->retcode;
		// Get exit code from the child that just finished			
		lock_release(waiterprocess->p_cv_lock);
	}else{
		// Waiter process has exited. Collect exit code
		lock_acquire(waiterprocess->p_cv_lock);
		// Get exit code
		*status = node->retcode;
		lock_release(waiterprocess->p_cv_lock); 	
	}

	/* In either case, the child process has yet to be destroyed. Do so here. */
	proc_destroy(waiterprocess);	// Call proc_destroy?
	

	*childpid = pid;

	return 0;

}

int
sys__exit(int exitcode){
	(void)exitcode;	

	// Find current pnode
	struct pnode *current;
	current = proc_get_pnode(curproc);	
	
	// If NULL, pnode does not exist (bad)
	if(current == NULL){
		return -1;
	}	

	// Check to see if process has a parent. Establish exit code and assign it in pnode.	
	if( curproc->parent != NULL ){
		// Process has parent; could be waited on so call proc_exited
		lock_acquire( curproc->p_cv_lock );
		current->retcode = _MKWAIT_EXIT(exitcode);
		proc_exited(curproc);
		cv_signal( curproc->p_cv, curproc->p_cv_lock );
		lock_release( curproc->p_cv_lock );
	}else{
		// Process is a parent, record exit code and proc_nuke
		lock_acquire( curproc->p_cv_lock );
		current->retcode = _MKWAIT_EXIT(exitcode);
		proc_destroy(curproc);
		lock_release( curproc->p_cv_lock );
	}	

	// Actually exit the process
	thread_exit();

	return 0;
}

int
sys_execv(char *program, userptr_t **args, int *retval){
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
	int num_args;
	
	// Set up argument array
	//char ** arglist;
	//arglist = (char **)kmalloc(sizeof(char *) * 64 );
	struct arg **arglist;
	arglist = (struct arg **)kmalloc(sizeof(struct arg *) * 64);

	
	/* Copy user arguments to the kernel, using a safe method (copyinstr) */
	int argcounter = 0;
	while(args[argcounter] != NULL && argcounter < 63){
		size_t inlength;
	
		// Reserve memory in arglist for the new string
		//arglist[argcounter] = kmalloc(sizeof(char) * NAME_MAX);
		arglist[argcounter] = (struct arg *)kmalloc(sizeof(struct arg *));
		arglist[argcounter]->str = kmalloc(sizeof(char) * NAME_MAX);
		// Copy string from userspace, update length
		//result = copyinstr((const_userptr_t)args[argcounter], arglist[argcounter], NAME_MAX, &inlength);
		result = copyinstr((const_userptr_t)args[argcounter], arglist[argcounter]->str, NAME_MAX, &inlength);
		arglist[argcounter]->len = inlength;

		kprintf("%s\n", arglist[argcounter]->str );

		argcounter++;
	}
	// Set total number of args and terminate the pointer string with NULL
	num_args = argcounter;
	arglist[argcounter] = NULL;

	/* arglist now holds all user arguments in "arg" structs */

	/* Open the file. */
	
	result = vfs_open((char *)program, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new process. */
	//KASSERT(proc_getas() == NULL);

	/* Set up stdin/stdout/stderr if necessary. */
	/*
	if (curproc->p_filetable == NULL) {
		curproc->p_filetable = filetable_create();
		if (curproc->p_filetable == NULL) {
			vfs_close(v);
			return ENOMEM;
		}

		result = open_stdfds("con:", "con:", "con:");
		if (result) {
			vfs_close(v);
			return result;
		}
	}
	*/

	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	proc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);
	
	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}

	/* Stackpointer contains ptr to beginning of user stack: where args need to go */
	/* Use copy safe methods (copyout) */
	argcounter = 0;
	while(arglist[argcounter] != NULL){
		size_t outlength;

		result = copyoutstr(arglist[argcounter]->str, (userptr_t)stackptr, NAME_MAX, &outlength);
		
		kprintf("%s\n", arglist[argcounter]->str);
		/* Shift stackptr by the length of the argument */
		stackptr -= outlength * sizeof(char);

		argcounter++;
		
	}
	


	/* Warp to user mode. */
	
	//enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
	//		  NULL /*userspace addr of environment*/,
	//		  stackptr, entrypoint);
	
	enter_new_process(num_args, (userptr_t)stackptr, NULL, stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	
	(void)retval;
	return EINVAL;

}

int
sys_getpid(int32_t *retval){
	// Defined in pr_table.c; function takes a process pointer and returns the PID associated with it
	
	*retval = proc_getpid(curproc);

	return 0;
}
