/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
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

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>

static bool pages_init = false;
static bool segments_init = false;

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

/* To create an address space, we need to:
 * (1) Allocate an addrspace using kmalloc
 * (2) Initialize struct variables
 *
 * (Note) Regions are first defined in as_define_region
 *
 */
struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	// Heap not generated yet
	as->area_heap_start = 0;
	as->area_heap_end = 0;

	return as;
}

/* Copy an address space */
/*
 *
 *
 *
 *
 *
 *
 */
int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	/*
	 * Write this.
	 */

	(void)old;

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */

	kfree(as);
}
/* Bring the current address space into the environment. The customer
 * has recieved their product!
 */
void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	/*
	 * Write this.
	 */
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 *
 * This is an important part of defining an addrspace. 
 * This function DEFINES the sections of an address
 * space including code areas ( but NOT stack and heap!). This
 * function does not allocate memory for the address space
 * regions just yet. Think of it as a purchase order for a 
 * specific address space.
 */

/* We need to:
 * (1) Initialize a new area struct for the segment.
 * (2) Compute the number of pages after page alignment.
 * (3) Update internal information, like permissions.
 * (4) Add the new area to the linked list.
 * (5) Update heap information based on vaddr and memsize
 */

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	(void)as;
	(void)readable;
	(void)writeable;
	(void)executable;
	struct area *newarea;

	unsigned int npages;

	// Page-alignment (rounding to the nearest page) -> from dumbvm.c
	memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;
	memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;
	npages = memsize / PAGE_SIZE;

	newarea = kmalloc(sizeof(struct area));
	if(newarea == NULL){
		return ENOMEM;
	}
	newarea->vstart = vaddr;
	newarea->pagecount = npages;
	newarea->next = NULL;

	// Bitpack options
	newarea->options = (readable<<2) & (writeable<<1) & (executable);
	
	// Add to linked list
	if(segments_init == false){		// First area is linked list head
		as->segments = newarea;
	
		segments_init = true;
	}else{					// Append to end of linked list	
		struct area *current;
		current = as->segments;

		while(current->next != NULL){
			current = current->next;	
		}

		current->next = newarea;
	}

	// Update heap information
	//as->area_heap_start = vaddr + memsize;
	//as->area_heap_start += memsize;
	
	return 0;
}

/* Using the segment information generated in as_define_region, allocate
 * memory for all of them. This is equivalent to actually executing the
 * purchase order; getting the parts and assembling them.
 */

/* Steps:
 * (1) Kmalloc pentry's for each region based on area->pagecount
 * (2) Bitpack each pentry's options.
 * (3) Actually reserve the pages. Need to use alloc_ppages() for pentry->paddr
 * (4) Update each pentry information set.
 * (5) Add each pentry to the addrspace's page table
 * (6) Reserve pages for the user stack
 */
int
as_prepare_load(struct addrspace *as)
{
	(void)as;

	if(pages_init == false){

		pages_init = true;
	}else{

	}

	return 0;
}

/* Continuing with my metaphor, this is pretty much shipping out/fulfulling
 * the purchase order. Last step is as_activate().
 */
int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

