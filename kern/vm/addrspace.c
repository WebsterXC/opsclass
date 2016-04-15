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
#include <spl.h>
#include <cpu.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>


/* ADDRESS SPACE IMPLEMENTATION */

/* Written by: William Burgin (waburgin) */

/* TODO
 * as_complete_load()
 * as_copy()
 * as_deactivate()
 */

/* To create an address space, we need to:
 * (1) Allocate an addrspace using kmalloc
 * (2) Initialize struct variables
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
	
	as->pages = NULL;
	as->segments = NULL;

	// Heap and stack not generated yet. Setting these to 1 is important for as_prepare_load.
	as->as_heap_start = 1;
	as->as_heap_end = 1;
	as->as_stackpbase = 1;
	as->as_heappbase = 1;

	return as;
}

/* Copy an address space. Needs to be loaded into memory */
/* (1) Create a new address space "object"
 * (2) Copy segments using as_define_region 
 * (3) Copy pages using as_prepare_load - identical segment info will generate the same pages
 * (4) Copy the data from the old address space pages to the new ones
 * 
 */
int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	int result;
	struct addrspace *newas;

	kprintf("as_copy()\n");
		
	if(old == NULL || ret == NULL){
		return EFAULT;
	}

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	// Get old segments and define them in the new address space
	if( old->segments != NULL){
		struct area *current;
		current = old->segments;

		while(current->next != NULL){			
			unsigned int opt = current->options;

			result = as_define_region(newas, current->vstart, current->bytesize, 
						  (opt^3)>>2, (opt^5)>>1, (opt^6));
			if(result){
				as_destroy(newas);
				return ENOMEM;
			}
			current = current->next; 
		}
	}else{
		newas->segments = NULL; 
	}	

	// Load pages for given segments. Create a stack and heap.
	// TODO If the process with oldas called sbrk() before trying to 
	// copy the addrspace, we may run into problems with heap sizes 
	// not matching.	
	result = as_prepare_load(newas);
	if(result){
		as_destroy(newas);
		return ENOMEM;
	}

	// Copy old addrspace page data over to the new addrspace pages
	struct pentry *sender;
	struct pentry *reciever;
	sender = old->pages;
	reciever = newas->pages;
	
	while( sender->next != NULL){
		memcpy((void *)reciever->paddr, (void *)sender->paddr, PAGE_SIZE);
		sender = sender->next;
		reciever = reciever->next;
	}

	*ret = newas;

	return 0;
}

/* We need to free memory for 3 distinct addrspace "parts":
 * (1) Page Table + Pages
 * (2) Segments/Regions
 * (3) The actual addrspace "object"
 */
void
as_destroy(struct addrspace *as)
{
	if(as == NULL){
		return;
	}

	struct pentry *terminate;
	struct pentry *second;

	// Destroy all pages inside of page table
	terminate = as->pages;
	while(second != NULL){
		second = terminate->next;
		kfree((void *)terminate->vaddr);
		terminate = second;
	}

	struct area *del;
	struct area *other;

	// Free segment partitions
	del = as->segments;
	while(other != NULL){
		other = del->next;
		kfree((void *)del->vstart);
		del = other;
	}

	// Just to be safe
	as->as_heap_start = 1;
	as->as_heap_end = 1;
	as->as_stackpbase = 1;
	as->as_heappbase = 1;

	kfree(as);
}
/* Bring the current address space into the environment. The customer
 * has recieved their product!
 * 
 * Note: If you kprintf in this method, you're gonna have a bad time...
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

	kprintf("as_activate()\n");

	// Shoot down all TLB Entries. This is from DUMBVM.
	vm_tlbshootdown_all();
	return;
}

void
as_deactivate(void)
{
	kprintf("deactivate_as\n");
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
	return;
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
 * specific address space that hasn't been fulfilled.
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
	if(as == NULL || vaddr == 0){
		return EFAULT;
	}

	struct area *newarea;
	unsigned int npages;

	// Page-alignment (rounding to the nearest page) -> from dumbvm.c
	memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;
	memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;
	npages = memsize / PAGE_SIZE;

	// Create a new region
	newarea = kmalloc(sizeof(struct area));
	if(newarea == NULL){
		return ENOMEM;
	}
	newarea->vstart = vaddr;
	newarea->pagecount = npages;
	newarea->next = NULL;
	newarea->bytesize = memsize;
	newarea->options = 0;

	// Bitpack options
	newarea->options = (readable<<2) & (writeable<<1) & (executable);
	
	// Add to linked list
	if(as->segments == NULL){		// First area is linked list head
		as->segments = newarea;
	}else{					// Append to end of linked list	
		struct area *current;
		current = as->segments;

		while(current->next != NULL){
			current = current->next;	
		}

		current->next = newarea;
	}

	return 0;
}

/* Using the segment information generated in as_define_region, allocate
 * memory for all of them. This is equivalent to actually executing the
 * purchase order; getting the parts and assembling them.
 */

// Helper function if adding phyiscal pages directly to the page table. This is used in
// as_prepare_load and returns the starting physical address of the region.
static paddr_t
add_table_entries(struct addrspace *as, vaddr_t start, unsigned int add, unsigned int seg_options, 
				unsigned int option_valid, unsigned int option_ref){
	
	paddr_t pstart;
	paddr_t preturn;
	pstart = alloc_ppages(add);
	preturn = pstart;
	if(pstart == 0){
		return 0;			
	}

	for(unsigned int i = 0; i < add; i++){
		struct pentry *entry;
		entry = kmalloc(sizeof(*entry));
		if(entry == NULL){
			return 0;
		}

		entry->paddr = pstart;			// Physical page
		if(as->as_stackpbase == 0){
			as->as_stackpbase = entry->paddr; // Set to 0 immidiately before calling
		}else if(as->as_heappbase == 0){
			as->as_heappbase = entry->paddr;
		}
	
		entry->vaddr = start;			// Virtual memory page maps to
		
		// Make pages writable so we can load information to them.		
		entry->options = (seg_options<<2) & (option_valid<<1) & option_ref;
		entry->next = NULL;

		if(as->pages == NULL){			// First page in table
			as->pages = entry;		
		}else{
			struct pentry *tail;
			tail = as->pages;

			while(tail->next != NULL){
				tail = tail->next;
			}
				
			tail->next = entry;
		}

		// Mapping 4K portions of regions (virtual addresses) to physical pages.
		start += PAGE_SIZE;
		pstart += PAGE_SIZE;
	}
	
	return preturn;
}

/* Steps:
 * (1) Kmalloc pentry's for each region based on area->pagecount
 * (2) Actually reserve the pages. Need to use alloc_ppages() for pentry->paddr
 * (3) Bitpack each pentry's options. 
 * (4) Update each pentry information set.
 * (5) Add each pentry to the addrspace's page table
 * (6) Reserve pages for the user stack. Preprocessor statement defines defualt stack pages.
 * (7) Reserve pages for the user heap. Preprocessor statement defines default heap size.
 */
int
as_prepare_load(struct addrspace *as)
{
	if(as == NULL){
		return EFAULT;
	}

	struct area *current;

	// Reserve pages for all user-defined regions
	current = as->segments;
	while(current->next != NULL){
	 
		// Steps (1) - (5) accomplished in this loop
		// VALID Bit: Has physical page been alloced for this vpage? YES
		// REFERENCED Bit: Has page been read/written to recently? NO
		current->pstart = add_table_entries(as, current->vstart, current->pagecount, current->options, 1, 0);
		if(current->pstart == 0){
			return ENOMEM;
		}

		current = current->next;
	}
	// Because of the way my while-loop is set up. We need the vstart of last region 
	// to create the heap location.
	current->pstart = add_table_entries(as, current->vstart, current->pagecount, current->options, 1, 0);
	if(current->pstart == 0){
		return ENOMEM;
	}

	/* Set the location of the heap for the addrspace. The user can call as_define_stack
	 * an "unlimited" number of times so we need to account for that. Since the heap
	 * begins immidiately after the last region, we bump the heap_start back each time
	 * this method is called.
	 */
	as->as_heap_start = current->vstart + (current->pagecount * PAGE_SIZE);
	as->as_heap_end = as->as_heap_start;
	//kprintf("Init heap size: %u bytes.\n", (as->as_heap_end - as->as_heap_start));	

/* This stuff needs to be moved to sbrk()
	vaddr_t heap_begin = current->vstart + (current->pagecount * PAGE_SIZE);
	as->as_heap_start = 0;
	as->as_heap_end = 0;
	as->as_heappbase = 0;
	result = add_table_entries(as, heap_begin, ADDRSP_HEAP_PAGES, 1, 1);
	if(!result){
		return ENOMEM;	
	}else if(as->as_heap_start == 0 || as->as_heap_end == 0 || as->as_heappbase == 0){
		panic("Critical failure: unable to generate a heap for addrspace.\n");
	}
*/

	return 0;
}

/* Continuing with my metaphor, this is pretty much shipping out/fulfulling
 * the purchase order. We're done loading information into our pages, so reset
 * their permissions to the original permissions.
 */
int
as_complete_load(struct addrspace *as)
{
	if(as == NULL){
		return EFAULT;
	}

	return 0;
}

/* Our purchase order wasn't able to be completed all in one department. Here
 * we finish up the order by sending it to the "stack department" to finish!
 *
 * We need to actually reserve stack pages here and return the top of the stack.
 * Essentially this is as_define_region exclusively for the stack.
 */
int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	int result;

	if(as == NULL || stackptr == NULL){
		return EFAULT;
	}
	
	// User stack begins at the number of default stack pages from REAR of addrspace.
	vaddr_t stack_begin = USERSTACK - (PAGE_SIZE * ADDRSP_STACKSIZE);
	as->as_stackpbase = 0;
	result = add_table_entries(as, stack_begin, ADDRSP_STACKSIZE, 7, 1, 1); 
	if(!result){
		return ENOMEM;
	}else if(as->as_stackpbase == 0){
		panic("Critical failure: unable to generate a stack for addrspace.\n");
	}

	// Ensure our stack is properly page-aligned after generation.
	KASSERT( (USERSTACK - stack_begin) % PAGE_SIZE == 0);

	//kprintf("Stack generation: %u Kbytes, %u pages.\n", (USERSTACK - stack_begin)/1024, ADDRSP_STACKSIZE); 

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;
	return 0;
}

