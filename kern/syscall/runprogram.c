/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009, 2014
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
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than runprogram() does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <current.h>
#include <synch.h>
#include <addrspace.h>
#include <lib.h>
#include <thread.h>
#include <mips/trapframe.h>
#include <pid.h>
#include <proc.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <openfile.h>
#include <filetable.h>
#include <syscall.h>
#include <test.h>
#include <copyinout.h>

/*
 * Open a file on a selected file descriptor. Takes care of various
 * minutiae, like the vfs-level open destroying pathnames.
 */
static
int
placed_open(const char *path, int openflags, int fd)
{
	struct openfile *newfile, *oldfile;
	char mypath[32];
	int result;

	/*
	 * The filename comes from the kernel, in fact right in this
	 * file; assume reasonable length. But make sure we fit.
	 */
	KASSERT(strlen(path) < sizeof(mypath));
	strcpy(mypath, path);

	result = openfile_open(mypath, openflags, 0664, &newfile);
	if (result) {
		return result;
	}

	/* place the file in the filetable in the right slot */
	filetable_placeat(curproc->p_filetable, newfile, fd, &oldfile);

	/* the table should previously have been empty */
	KASSERT(oldfile == NULL);

	return 0;
}

/*
 * Open the standard file descriptors: stdin, stdout, stderr.
 *
 * Note that if we fail part of the way through we can leave the fds
 * we've already opened in the file table and they'll get cleaned up
 * by process exit.
 */
static
int
open_stdfds(const char *inpath, const char *outpath, const char *errpath)
{
	int result;

	result = placed_open(inpath, O_RDONLY, STDIN_FILENO);
	if (result) {
		return result;
	}

	result = placed_open(outpath, O_WRONLY, STDOUT_FILENO);
	if (result) {
		return result;
	}

	result = placed_open(errpath, O_WRONLY, STDERR_FILENO);
	if (result) {
		return result;
	}

	return 0;
}

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname)
{
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new process. */
	KASSERT(proc_getas() == NULL);

	/* Set up stdin/stdout/stderr if necessary. */
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

	/* Warp to user mode. */
	enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}

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
	
	lock_acquire(gpll_lock);

	//kprintf("Num: %d\n", proc_rollcall() );	
	/* Generate a "fork image" to pass to the child handle function.
	 * A fork image consists of heap copies of the current process' address
	 * space, trapframe, and filetable. 
	 */
	if( proc_rollcall() > 64){
		lock_release(gpll_lock);
		//proc_nuke(curproc);
		sys__exit(1);
		return EMPROC;
	}
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
		kfree(fk_img);
		return ENOMEM;
	}
	as_copy(curproc->p_addrspace, &childproc->p_addrspace);
	
	lock_release(gpll_lock);

	// Fork the process through the child() method above, associated with childproc.
	result = thread_fork(curproc->p_name, childproc, child, fk_img, 0);  	 
	
	if(result == ENOMEM){
		kfree(fk_img);
		return ENOMEM;
	}else if(result){
		kfree(fk_img);
		return -1;
	}

	// Return with child's PID
	*childpid = proc_getpid(childproc);
	
	return 0;
}

int
sys_waitpid(pid_t pid, int *status, int options, int *childpid){
	(void)options;
	bool setdestroy = false;

	/* If status is NULL, waitpid behaves the same just doesn't give
	 * child exit code.
	 */
	if(status == NULL){
		status = kmalloc(sizeof(*status));
		setdestroy = true;
	}

	/* Check all of the args for errors */
	if( pid < __PID_MIN || pid > __PID_MAX ){
		return ESRCH;
	}

	/* Get waiter process pnode */
	struct proc *waiterprocess;
	struct pnode *node;
	waiterprocess = proc_getptr(pid);
	node = proc_get_pnode(waiterprocess);
	

	/* Determine if waiter process exists */
	if( waiterprocess == NULL || node == NULL ){
		return ECHILD;	
	}

	waiterprocess->parent = curproc;
	
	/* Determine if waiter process has finished */
	if( waiterprocess->isactive == true ){
		//kprintf("Waiting on: %d\n", node->pid);
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

	// Free the fake status ptr if status was originally NULL
	if(setdestroy == true){
		status = NULL;
		kfree(status);
	}

	/* In either case, the child process has yet to be destroyed. Do so here. */
	proc_destroy(waiterprocess);	// Call proc_destroy?

	*childpid = pid;

	return 0;

}

int
sys__exit(int exitcode){

	// Find current pnode
	struct pnode *current;
	current = proc_get_pnode(curproc);	
	
	// If NULL, pnode does not exist (bad)
	if(current == NULL){
		return -1;
	}	

	// Check to see if process has a parent. Establish exit code and assign it in pnode.	
	if( curproc->parent != NULL ){
		//kprintf("Signal to: %d\n", current->pid);
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
		//proc_nuke(curproc);
		lock_release( curproc->p_cv_lock );
		proc_destroy(curproc);
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
	size_t pr_length;

	if(program == NULL){
		*retval = -1;
		return EFAULT;
	}

	lock_acquire(gpll_lock);

	char *pr_name;
	pr_name = kmalloc(PATH_MAX * sizeof(char));
	
	int num_args = 0;
	while( args[num_args] != NULL ){
		num_args++;
	}


	char **bigbuffer = (char **)kmalloc(sizeof(char *) * num_args);	
	if( bigbuffer == NULL ){
		*retval = -1;
		lock_release(gpll_lock);
		return ENOMEM;
	}

	// Get program name
	copyinstr((userptr_t)program, pr_name, PATH_MAX, &pr_length);

	/* Copy user arguments to the kernel, using a safe method (copyinstr) */
	int argcounter = 0;
	int memsize = 0;
	while(args[argcounter] != NULL && argcounter < 4999){
		size_t inlength;	// Input length from copyinstr()	
		char *tempstr;
		char *bufstr;
		tempstr = kmalloc(sizeof(char) * ARG_MAX);

		// Copy string from userspace, update length; ALL RESULTS INCLUDE NULL TERMINATOR
		result = copyinstr((const_userptr_t)args[argcounter], tempstr, ARG_MAX, &inlength);
		
		bufstr = kmalloc(sizeof(char) * inlength);
		memsize += inlength * sizeof(char);		

		// Memory Check #1
		if(bufstr == NULL || tempstr == NULL || memsize >= ARG_MAX){
			*retval = -1;
			lock_release(gpll_lock);
			return ENOMEM;
		}

		strcpy(bufstr, tempstr);
		bigbuffer[argcounter] = bufstr;

		kfree(tempstr);
		argcounter++;
	}
	// Set total number of args and terminate the pointer string with NULL
	bigbuffer[num_args] = NULL;

	/* Open the file. */
	
	result = vfs_open(pr_name, O_RDONLY, 0, &v);
	if (result) {
		*retval = -1;
		return ENOENT;
	}
	kfree(pr_name);
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
	//argcounter = num_args-1;
	argcounter = 0;
	//kprintf("Potential nomem\n");
	vaddr_t stacksonstacks[num_args];
	//char *stacksonstacks;
	//stacksonstacks = kmalloc(sizeof(*stacksonstacks) * num_args);
		
	while(bigbuffer[argcounter] != NULL){
	//while(argcounter >= 0){	
		size_t outlen;
		int mod;
		int adjust;
		int length;
	
		length = strlen(bigbuffer[argcounter]) + 1;

		// Copy adjusted arguments
		mod = length % 4;
			if( mod == 0 ){
				adjust = 0;
			}else{		
				adjust = 4 - mod;
			}
			

		//stackptr -= arglist[argcounter]->len;
		stackptr -= length;
		stackptr -= adjust;
		//kprintf("Adjust: %d\n", arglist[argcounter]->len + adjust);
		
		copyoutstr(bigbuffer[argcounter], (userptr_t)stackptr, ARG_MAX, &outlen);
		kfree(bigbuffer[argcounter]);
				
		/* Shift stackptr by the length of the argument */
		/* Put a copy of the stackptr in the array */

		stacksonstacks[argcounter] = stackptr;		

		//argcounter--;
		argcounter++;
	}
	// User args are still NULL terminated	
	stackptr -= 4;
	copyout(bigbuffer[num_args], (userptr_t)stackptr, 4);

	// Stackpointer still needs references to where args are on the stack.
	// (thanks Tuesday office hour crew!!)
	for(int i = num_args; i > 0; i--){
		stackptr -= 4;
		result = copyout(&stacksonstacks[i-1], (userptr_t)stackptr, 4);
	}
		//kprintf("Stackptr: %x\n", stackptr);
	
		//stackptr += 4;
		//stackptr-=4;
	/* Warp to user mode. */

	//enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
	//		  NULL /*userspace addr of environment*/,
	//		  stackptr, entrypoint);

	*retval = 0;

	//kfree(stacksonstacks);
	kfree(bigbuffer);	
	lock_release(gpll_lock);

	enter_new_process(num_args, (userptr_t)stackptr, NULL, stackptr, entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	
	return 0;

}

int
sys_getpid(int32_t *retval){
	// Defined in pr_table.c; function takes a process pointer and returns the PID associated with it
	
	*retval = proc_getpid(curproc);

	return 0;
}
	

