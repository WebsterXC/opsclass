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
/* Being an ex-FrozenCPU employee, I love making metaphors based on the
 * daily processes we had there because it was so defined and clear-cut.
 * In fact, establishing an address space closesly resembles any retail/
 * warehouse environment! For an address space, I use a metaphor of carrying
 * out a purchase order.
 */

/* Our purchase order request has been recieved! Clean the packing
 * station for the items that are to come.
 *
 * To create an address space, we need to:
 * (1) Allocate an addrspace using kmalloc
 * (2) Initialize struct variables to 0 or NULL
 */
struct addrspace *
as_create(void)
{
	struct addrspace *as;
	
	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}
	
	as->segments = NULL;

	as->as_heap_start = 0;
	as->as_heap_end = 0;

	as->stack = NULL;
	as->heap = NULL;

	return as;
}

/* Function assists in creating page table entries and adding them to the segment's linked list.
 * Note that this function applies only to segments, and not to the stack or heap page tables.
 */
static void
add_table_entries(struct area *segment, vaddr_t start, unsigned int add){
	
	// Make pentries for the number of pages needed
	for(unsigned int i = 0; i < add; i++){
		struct pentry *entry;
		entry = kmalloc(sizeof(*entry));
		if(entry == NULL){
			return;
		}
	
		// Initialize struct variables with the information we have so far
		entry->vaddr = start;
		entry->paddr = 0;
		entry->next = NULL;

		if(segment->pages == NULL){			// First page in table
			segment->pages = entry;
		}else{
			struct pentry *tail;
			tail = segment->pages;

			while(tail->next != NULL){
				tail = tail->next;
			}

			tail->next = entry;
		}

		// Mapping 4K portions of regions (virtual addresses) to physical pages.
		start += PAGE_SIZE;
	}
	return;
}

/* Copy all pages in an already initialized segment and prepare it for addition to a linked list. */
static int
seg_copy(struct area **out, struct area *src){
	
	struct pentry *copyable;

	struct area *dest;
	dest = (struct area *)kmalloc(sizeof(*dest));
	if(dest == NULL){
		return ENOMEM;	
	}
	// Copy over segment information
	dest->vstart = src->vstart;
	dest->pagecount = src->pagecount;
	dest->bytesize = src->bytesize;
	dest->next = NULL;
	dest->pages = NULL;
	
	/* Copy all pages in the segment. Copying a page is more than just transferring struct
	 * variables, we need to be an EXACT copy of the src, so we need to use memmove to transfer
	 * bytes to the dest. It's important to use memmove, as memcpy has the potential of stepping
	 * on memory.
	 */
	copyable = src->pages;
	while(copyable != NULL){
		struct pentry *newpage;
		newpage = (struct pentry *)kmalloc(sizeof(*newpage));	
		if(newpage == NULL){
			return ENOMEM;
		}
		
		/* I had to compile over 150+ times to recognize that you can't transfer information
		 * unless the physical memory for dest has already been set aside. Since each physical
		 * page maps to a kernel virtual address, we need to convert it for copying.
		 */
		newpage->paddr = alloc_ppages(1);
		if(newpage->paddr == 0){
			kfree(newpage);
			return ENOMEM;
		}
		memmove((void *)PADDR_TO_KVADDR(newpage->paddr), (const void *)PADDR_TO_KVADDR(copyable->paddr), PAGE_SIZE);
		// Copy over the rest of the struct info
		newpage->vaddr = copyable->vaddr;
		newpage->next = NULL;

		// Add the page table entry to the new segment's pages
		if(dest->pages == NULL){
			dest->pages = newpage;
		}else{

			struct pentry *find;
			find = dest->pages;
			while(find->next != NULL){
				find = find->next;
			}
			find->next = newpage;
		}

		copyable = copyable->next;
	}

	*out = dest;
	return 0;
}
/* Copy an address space. This is really the heart of the VM assignment, excluding
 * the vm_fault function. Personally, I think as_copy is the more difficult of the 
 * two. If you don't understand MIPS memory mappings now, better learn quick! :-)
 */
/* (1) Create a new address space "object"
 * (2) Copy each segment that was generated in as_define_region
 * (3) Foreach segment, copy each page as well as the raw bytes from each page.
 * (4) Generate a new stack for the new addrspace and copy over raw bytes.
 * (5) If a heap exists (without sbrk(), it won't), copy pages and raw bytes.
 * (6) Set the heap breakpoints equal to the old addrspace's breakpoints.
 */
int
as_copy(struct addrspace *old, struct addrspace **ret)
{	
	int result;
	struct addrspace *newas;

	// Basic argument checking
	if(old == NULL || ret == NULL){
		return EFAULT;
	}


	/* Check the integrity of our arguments. Although in some
	 * cases, copying an addrspace before calling as_define_region
	 * might be necessary, for the purpose of CSE421, it's not
	 * necessary and could signify an error with my implementation.
	 */
	if(old->segments == NULL || old->segments->pages == NULL){
		panic("as_copy discovered NULL region/page information, try again n00b\n");
	}

	// Create a new address space
	newas = as_create();
	if (newas == NULL) {
		return ENOMEM;
	}
	
	/* The core functionality of as_copy. We need to copy the
	 * address space's segment information. However, since the
	 * function must return an EXACT copy of the address space
	 * in question, all page information must be copied over too.
	 */
	struct area *oldseg;
	KASSERT(old->segments != NULL);
	oldseg = old->segments;
	while( oldseg != NULL ){
		struct area *newseg;	
	
		result = seg_copy(&newseg, oldseg);
		if(result){
			return ENOMEM;
		}
		KASSERT(newseg->pages != NULL);	

		newseg->next = NULL;

		// Append to linked list
		if(newas->segments == NULL){
			newas->segments = newseg;
		}else{
			struct area *tail;
			tail = newas->segments;
			while(tail->next != NULL){
				tail = tail->next;
			}
				
			tail->next = newseg;
		}

		oldseg = oldseg->next;
	}

	// Ensure we set everything up correctly	
	KASSERT(newas->segments != NULL);

	// Copy the stack by first making pentries and then copying information
	vaddr_t fakestack;
	result = as_define_stack(newas, &fakestack);	
	if(result){
		as_destroy(newas);
		return EFAULT;
	}

	
	struct pentry *oldstack;
	struct pentry *newstack;
	
	oldstack = old->stack;
	newstack = newas->stack;

	/* Here we copy over the stack information. Memmove is like sudo; it will
	 * do whatever you ask it to do, regardless of if you know what you're doing.
	 * The if-statement ensures were only copying over stack pages that have been
	 * allocated by our on-demand pager in vm_fault. Without it, memmove would try
	 * to copy from address 0x0 and just sit there and hang.
	 */
	while( oldstack != NULL ){
		if(oldstack->paddr != 0){
			newstack->paddr = alloc_ppages(1);
			memmove((void *)PADDR_TO_KVADDR(newstack->paddr), (const void *)PADDR_TO_KVADDR(oldstack->paddr), PAGE_SIZE);
		}

		oldstack = oldstack->next;
		newstack = newstack->next;
	}

	// Copy the heap. If malloc() hasn't been called in userspace, this loop is skipped.
	struct pentry *oldheap;
	struct pentry *newheap;

	oldheap = old->heap;
	newheap = newas->heap;
	while( oldheap != NULL ){
		if(oldheap->paddr != 0){
			newheap->paddr = alloc_ppages(1);
			memmove((void *)PADDR_TO_KVADDR(newheap->paddr), (const void *)PADDR_TO_KVADDR(oldheap->paddr), PAGE_SIZE);
		}		

		oldheap = oldheap->next;
		newheap = newheap->next;
	}
	
	// Set heap breakpoints from old addrspace
	newas->as_heap_start = old->as_heap_start;
	newas->as_heap_end = old->as_heap_end;
	

	*ret = newas;
	return 0;
}

/* The customer messed up and wants to cancel the order. We need to put
 * all the items back on the shelf...
 *
 * We need to free memory for 4 distinct addrspace "parts":
 * (1) Segments (from as_define_region)
 * (2) Stack
 * (3) Heap
 * (4) The actual addrspace "object".
 */
void
as_destroy(struct addrspace *as)
{
	if(as == NULL){
		return;
	}


	struct area *seg;
	struct area *move;
	
	// Free all segments
	seg = as->segments;
	while(seg != NULL){
		struct pentry *upages;
		struct pentry *temp;

		// Free all pages in each segment
		upages = seg->pages;
		while(upages != NULL){
			temp = upages->next;
			free_ppage(upages->paddr);
			kfree(upages);
			upages = temp;
		}	

		move = seg->next;
		kfree(seg);
		seg = move;
	}

	// Free all stack pages
	struct pentry *freestack;

	freestack = as->stack;
	while(freestack != NULL){
		struct pentry *temp;

		temp = freestack->next;
		free_ppage(freestack->paddr);
		kfree(freestack);
		freestack = temp;
	}	

	// Free all heap pages
	struct pentry *freeheap;

	freeheap = as->heap;
	while(freeheap != NULL){
		struct pentry *temp;

		temp = freeheap->next;
		free_ppage(freeheap->paddr);
		kfree(freeheap);
		freeheap = temp;
	}	

	// Just to be safe
	as->as_heap_start = 0;
	as->as_heap_end = 0;

	kfree(as);
	return;	
}
/* Bring the current address space into the environment. The customer
 * has recieved their product!
 *
 * All we need to do is flush the TLB entries from existence. A new
 * address space maps different vaddrs to paddrs, and the mappings
 * already stored aren't relevant.
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

	// Shoot down all TLB Entries. See vm.c.
	vm_tlbshootdown_all();
	return;
}

/* This doesn't really fit into my metaphor...
 * See below. Based on my implementation, I didn't
 * need to implement this.
 */
void
as_deactivate(void)
{
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
 * regions just yet.
 *
 * Our purchase order is ready to be run. Suppose that it's very
 * large and requires multiple employees to complete successfully;
 * we're giving assignments to each employee so they know what items
 * to grab.
 */

/* Note that I use the terms segment and region interchangeably:
 * (1) Align the memsize and vaddr with the page size. Recall that
 * the entire point of paging was so that all information fits inside
 * of pages. This logic can be found in arch/mips/dumbvm.c
 * (2) Initialize a new segment "object" for the segment.
 * (3) Fill the segment struct information.
 * (4) Add the new area to the linked list.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	(void)readable;
	(void)writeable;
	(void)executable;

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
	
	// Create a new segment
	newarea = kmalloc(sizeof(struct area));
	if(newarea == NULL){
		return ENOMEM;
	}
	newarea->vstart = vaddr;
	newarea->pagecount = npages;
	newarea->bytesize = memsize;
	newarea->pages = NULL;
	newarea->next = NULL;
		
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

/* All of our employees have gotten their assignments from as_define_region
 * and come back with all the items necessary to fulfill the order. We haven't
 * removed any of the items from stock yet though!
 */

/* We need to:
 * (1) Kmalloc pentry's for each region based on area->pagecount
 * (2) Update each pentry's struct variables, like RWX options.
 * (3) Add each pentry to the addrspace's page table
 */
int
as_prepare_load(struct addrspace *as)
{
	if(as == NULL){
		return EFAULT;
	}

	struct area *current;
	current = as->segments;

	// Trying to load an address space with no static code region makes no sense.
	KASSERT(as->segments != NULL);

	while(current != NULL){
		// Generate pentries for required pages. Pages aren't reserved until a Page Fault.
		add_table_entries(current, current->vstart, current->pagecount);
		
		// The heap begins immdidiately after the last segment, but has size 0 initially.
		as->as_heap_start = current->vstart + (current->pagecount * PAGE_SIZE);
		as->as_heap_end = as->as_heap_start;

		current = current->next;
	}
	
	KASSERT(as->as_heap_start != 0 && as->as_heap_end != 0);

	return 0;
}

/* If I was actually using the options field of my address space, I'd reset
 * the options to their original values.
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

	if(as == NULL || stackptr == NULL){
		return EFAULT;
	}
	
	// User stack begins at the number of default stack pages from REAR of addrspace.
	vaddr_t stack_begin = USERSTACK - (PAGE_SIZE * ADDRSP_STACKSIZE);
	
	// Assigned pentry's for our stack (as->stack)
	for(int i = 0; i < ADDRSP_STACKSIZE; i++){
		KASSERT(stack_begin < USERSTACK);
		struct pentry *newpage;
		newpage = kmalloc(sizeof(*newpage));
		if(newpage == NULL){
			return ENOMEM;
		}
		newpage->vaddr = vaddr_to_vpn(stack_begin);
		newpage->paddr = 0;
		newpage->next = NULL;

		if( as->stack == NULL ){
			as->stack = newpage;
		}else{
			struct pentry *traverse;
			traverse = as->stack;
			while(traverse->next != NULL){
				traverse = traverse->next;
			}
			traverse->next = newpage;
		}
	
		stack_begin += PAGE_SIZE;
	}
	
	*stackptr = USERSTACK;
	return 0;
}

