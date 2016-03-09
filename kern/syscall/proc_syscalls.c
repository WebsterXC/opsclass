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

static int rev_and_append(char*, char*, int);

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
		kfree(fk_img);
		return ENOMEM;
	}
	as_copy(curproc->p_addrspace, &childproc->p_addrspace);
	
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
	
	/* Determine if waiter process has finished */
	if( waiterprocess->isactive == true ){
		kprintf("Waiting on: %d\n", node->pid);
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
		kprintf("Signal to: %d\n", current->pid);
		// Process has parent; could be waited on so call proc_exited
		lock_acquire( curproc->p_cv_lock );
		current->retcode = exitcode;
		proc_exited(curproc);
		cv_signal( curproc->p_cv, curproc->p_cv_lock );
		lock_release( curproc->p_cv_lock );
	}else{
		// Process is a parent, record exit code and proc_nuke
		lock_acquire( curproc->p_cv_lock );
		current->retcode = exitcode;
		//proc_destroy(curproc);
		proc_nuke(curproc);
		lock_release( curproc->p_cv_lock );
	}	

	// Actually exit the process
	thread_exit();

	return 0;
}

/* Used while handling execv arguments. Function takes a traditional, null-terminated arg inside of src and
 * reverses it. It appends the required amount of padding needed to the front of this new string
 * (denoted by need) and then places the return in **ret. Returns the length of the new string including leading '\0'
 * NOTE: need should never be 0; even str%4==0 requires a null terminator to begin with
 */
static int
rev_and_append(char *src, char *ret, int need){
	int retcounter = 0;

	// Even strings that can mod4 need at least 1 null term
	ret[retcounter] = '\0';
	retcounter++;

	// Add ADDITIONAL null's if needed
	if( need != 0) {
		int addnull = 4-need;
		// Begin by appending required number of '\0' characters.
		for(int i =0; i < addnull; i++){
			ret[retcounter] = '\0';
			retcounter++;
		}
	}

	// Now reverse the src string by adding it to *ret in reverse order
	int srccounter = strlen(src);		// DOES NOT include null terminator
	for(int i = srccounter; i > 0; i--){
		ret[retcounter] = src[srccounter-1];
		retcounter++;
		srccounter--;
	}

	return retcounter;
}

int
sys_execv(char *program, userptr_t **args, int *retval){
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
	int num_args;
	
	// Set up argument array
	struct arg **arglist;
	arglist = (struct arg **)kmalloc(sizeof(struct arg *) * 64);

	// Set up array to hold stackpointers for each argument
	//vaddr_t **stacksonstacks;
	//stacksonstacks = (vaddr_t **)kmalloc(sizeof(vaddr_t *) * 64);	
	
	vaddr_t *stacksonstacks;
	stacksonstacks = kmalloc(sizeof(*stacksonstacks) * 64 );

	//char **buffer;
	//buffer = (char **)kmalloc(sizeof(char *) * 128);

	/* Copy user arguments to the kernel, using a safe method (copyinstr) */
	int argcounter = 0;
	while(args[argcounter] != NULL && argcounter < 63){
		size_t inlength;	// Input length from copyinstr()
		unsigned int append;	// Number of nullchars to append to the string
		char *tempstr;		// Temporary string to hold copyinstr(), but before reversal
		tempstr = kmalloc(sizeof(*tempstr));	

		// Reserve memory in arglist for the new string
		arglist[argcounter] = (struct arg *)kmalloc(sizeof(struct arg *));
		arglist[argcounter]->str = kmalloc(sizeof(char) * NAME_MAX);
		
		// Copy string from userspace, update length; ALL RESULTS INCLUDE NULL TERMINATOR
		result = copyinstr((const_userptr_t)args[argcounter], tempstr, NAME_MAX, &inlength);
		//kprintf("%s| with length: ", tempstr );
			
		// Based on null-inclusive length, get the result of length%4 and pass to rev_and_append()	
		append = inlength % 4;
		arglist[argcounter]->len = rev_and_append(tempstr, arglist[argcounter]->str, append ); 
		
		kfree(tempstr);
		argcounter++;
	}
	// Set total number of args and terminate the pointer string with NULL
	num_args = argcounter;
	arglist[argcounter] = NULL;


	/* Open the file. */
	
	result = vfs_open(program, O_RDONLY, 0, &v);
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
		
		// Arguments are copied onto the stack backwards

		// Copy adjusted arguments
		result = copyout(arglist[argcounter]->str, (userptr_t)stackptr, arglist[argcounter]->len * sizeof(char) );
		//memcpy(buffer[argcounter], arglist[argcounter]->, arglist[argcounter]->len * sizeof(char) );

		kprintf("StackPtr: %x | Len: %d | Final: ", stackptr, arglist[argcounter]->len);
		/* Shift stackptr by the length of the argument */
		/* Put a copy of the stackptr in the array */
		stackptr -= arglist[argcounter]->len * sizeof(char);
		//stacksonstacks[argcounter] = kmalloc(sizeof(vaddr_t *));
		stacksonstacks[argcounter] = stackptr;
		kprintf("%x\n", stackptr);
		//kprintf("Shift: %d\n", arglist[argcounter]->len);
		argcounter++;
	
	}
	// User args are still NULL terminated
	//result = copyout(arglist[argcounter], (userptr_t)stackptr, 4);
	//stackptr -= 4;
	char arr[4] = {'\0', '\0', '\0', '\0'};
	copyout(&arr, (userptr_t)stackptr, 4);	
	//memcpy(buffer[argcounter], &arr, 4);
	stackptr -= 4;

	// Stackpointer still needs references to where args are on the stack.
	// (thanks Tuesday office hour crew!!)
	
	for(int i = num_args; i > 0; i--){
		stackptr -= 4;
		argcounter++;
		kprintf("Arg %d at: %x\n", i-1, (int)stacksonstacks[i-1]);
		result = copyout(&stacksonstacks[i-1], (userptr_t)stackptr, 4);
		//memcpy(buffer[argcounter], &stacksonstacks[i-1], 4);

		//stackptr -= sizeof(&stacksonstacks[i-1]);
	}
	
	/* Warp to user mode. */

	//enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
	//		  NULL /*userspace addr of environment*/,
	//		  stackptr, entrypoint);
	
	enter_new_process(num_args, (userptr_t)stackptr, NULL, stackptr, entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	
	(void)retval;
	return 0;

}

int
sys_getpid(int32_t *retval){
	// Defined in pr_table.c; function takes a process pointer and returns the PID associated with it
	
	*retval = proc_getpid(curproc);

	return 0;
}
