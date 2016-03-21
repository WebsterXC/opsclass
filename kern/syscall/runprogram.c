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
 * Data1: Pointer to forkable trapframe
 * Data2: Unused.
 */
void
child(void *tf, long unsigned int data2){
	(void)data2;

	// Block until the parent process has finished copying filetable
	P(curproc->forksem);

	// Load and activate the address space	`	
	proc_setas( curproc->p_addrspace );	
	as_activate();	
	
	// Pass trapframe, separate stack copy made in method
	// See: arch/mips/syscall/syscall.c
	enter_forked_process( (struct trapframe *)tf );

}

int
sys_fork(struct trapframe *frame, int32_t *childpid){

	/* New process and trapframe copies will be made */
	struct proc *childproc;
	struct trapframe *trap;
	int result;		
		
	lock_acquire(gpll_lock);	

	// 9 or fewer processes at once (memory managment)
	while( proc_rollcall() > 10 ){
		cv_wait(gpll_cv, gpll_lock);
	}

	// Make a copy of the trapframe on the heap
	trap = kmalloc(sizeof(*trap));
	if(trap == NULL){
		lock_release(gpll_lock);
		return ENOMEM;
	}
	memcpy(trap, frame, sizeof(*frame));

	// Generate an exact copy of this process and copy the current
	// process' addrspace to the copy
	result = proc_fork(&childproc);
	as_copy(curproc->p_addrspace, &childproc->p_addrspace);
	
	lock_release(gpll_lock);
	
	if(result){
		return ENOMEM;
	}
	
	// Fork the process. Copy the filetable over to the child and increment
	// the child's semaphore so it knows to continue the fork.
	result = thread_fork(curproc->p_name, childproc, child, trap, 0);  	 

	filetable_copy(curproc->p_filetable, &childproc->p_filetable);
	
	V(childproc->forksem);

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
	(void)options;


	/* Semaphore = 20 Bytes */
	/* Lock      = 24 Bytes */
	/* Condition = 16 Bytes */

	/* Check all of the args for errors */
	if( pid < __PID_MIN || pid > __PID_MAX ){
		return ESRCH;
	}

	/* Get waiter process pnode */
	struct proc *waiterprocess;
	struct pnode *childnode;
	
	lock_acquire(gpll_lock);
	
	/* Get process to wait on and it's respective pnode in the GPLL */
	waiterprocess = proc_getptr(pid);
	childnode = proc_get_pnode(waiterprocess);

	/* Determine if waiter process exists */
	if( waiterprocess == NULL || childnode == NULL ){
		return ECHILD;	
	}

	// Controls whether or not _exit() destroys the process. In this
	// case, we will manually destroy it after waiting.
	waiterprocess->parent = curproc;

	lock_release(gpll_lock);

	P(childnode->exitsem);
	if(status != NULL){
		*status = childnode->retcode;
	}
	
	// Destroy the process we were waiting on since it's confirmed exited.
	proc_destroy(waiterprocess);

	*childpid = pid;

	return 0;

}

int
sys__exit(int exitcode){

	// Find current pnode of the process to exit
	struct pnode *current;
	current = proc_get_pnode(curproc);	
	
	// If NULL, pnode does not exist (bad)
	if(current == NULL){
		return -1;
	}	

	lock_acquire(gpll_lock);

	/* Generate the current process' exit code and increment the
	 * exit semaphore to let waitpid() know curproc has exited.
	 */
	current->retcode = _MKWAIT_EXIT(exitcode);
	V(current->exitsem);	

	// If this process called exit on it's own (without waitpid), just destroy it
	if( curproc->parent == NULL ){
		proc_destroy(curproc);
	}

	// Signal the GPLL CV to let it know memory has been freed for more forks.
	cv_signal(gpll_cv, gpll_lock);
	lock_release(gpll_lock);

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
	kprintf("Execv\n");
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
		kprintf("Couldn't allocate bigbuffer\n");
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
		tempstr = kmalloc(sizeof(char) * ARG_MAX );
		if(tempstr == NULL){
			lock_release(gpll_lock);
			kprintf("tempstr kmalloc\n");
			return ENOMEM;
		}		

		// Copy string from userspace, update length; ALL RESULTS INCLUDE NULL TERMINATOR
		result = copyinstr((const_userptr_t)args[argcounter], tempstr, ARG_MAX, &inlength);
		
		//bufstr = kmalloc(sizeof(char) * inlength);
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
	kprintf("Potential nomem\n");
	vaddr_t stacksonstacks[num_args];
	//char *stacksonstacks;
	//stacksonstacks = kmalloc(sizeof(*stacksonstacks) * num_args);
	kprintf("After nonmem\n");	
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
	

