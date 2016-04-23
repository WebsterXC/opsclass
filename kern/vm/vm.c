/* Virtual Memory Implementation */
/* By: William Burgin */


#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <mainbus.h>

static unsigned long corecount;		// Number of total cores
static bool stay_strapped = false;	// Has vm_bootstrap run yet?

static volatile unsigned int total_page_allocs = 0;	// Total pages currently allocated
static struct core *coremap;				// Pointer to coremap

/* Virtual Memory Wizardry*/

/* Here we need to create the coremap to store physical page info.
 * This requires a few steps:
 * (1) Setup coremap resources
 * (2) Find available RAM based on first and last addresses of available memory.
 * (3) Get total number of pages. Available RAM (last-first)/page size (4K).
 * (4) Manually allocate the coremap, get it's starting paddr. Assign to PADDR_TO_KVADDR
 * (5) Iterate through coremap and initialize core information.
 * (6) Set up global coremap lock. --> Set up statically. Step no longer needed.
 */

static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;	// Synchro primitive for coremap

void
vm_bootstrap(void){
	if(stay_strapped == true){
		return;
	}

	paddr_t first;
	paddr_t last;
	(void)last;	
	unsigned int num_cores;		// Total number of free pages
	unsigned int map_size;		// Size of the coremap (bytes)

	// Reserve memory for a coremap lock.
	// Switched to static initializer above.	

	// Get last and first address of RAM.
	// *** ram_stealemem will no longer work ***
	last = ram_getsize();
	first = ram_getfirstfree(); 

	// Get number of available cores, and size of coremap (in bytes)
	num_cores = (last - first) / PAGE_SIZE;

	KASSERT(num_cores != 0);
	corecount = num_cores;
	map_size = num_cores * sizeof(struct core);
	
	// Convert paddr to a kernel virtual address where coremap starts
	coremap = (struct core *)PADDR_TO_KVADDR(first);
	kprintf("Num Pages %d at %u\n", num_cores, first);	
	
	// Set all coremap cores to fixed state, others to free
	for(unsigned int i = 0; i < num_cores; i++){
		// If coremap lies on this core, it's fixed in place
		if( i < DIVROUNDUP(map_size, PAGE_SIZE) ){
			coremap[i].state = COREMAP_FIXED;
			// Make sure to register the coremap takes +1 pages to store
			total_page_allocs++;		// Don't even think about moving this	
		}else{	
			coremap[i].state = COREMAP_FREE;
		}

		/*Fill the other struct information */	
	
		//Signifies the end core of an allocation
		coremap[i].istail = false;	
	
		//The physical address where we're located
		coremap[i].paddr = first;

		//The kernel vaddr that we map to
		coremap[i].vaddr = PADDR_TO_KVADDR(first);

		//Manually increment by bytes to next page for the next iteration's paddr
		first += PAGE_SIZE;
	}
		
	stay_strapped = true;
	return;
}

/* Allocate a certain number of pages */
/* This also requires several steps to accomplish:
 * (1) Acquire the spinlock.
 * (2) Check to see if we're bootstrapped. Use ram_stealmem() if we're not.
 * (3) If npages > 1, search for a contiguous block that fits the requested size.
 * (4) Update each core of the allocation. Label the tail core!
 * (5) Release the spinlock. 
 * (6) Return PADDR_TO_KVADDR of the beginning of the allocation.
 */

/* Currently, the double for-loop iterates through the entire coremap. Once working,
 * optimize to jump to the end of a non-free memory allocation and continue
 * searching on a contig fail. Function returns 0 on failure and should be
 * checked before proceeding in the function it's called in. Normal operation
 * returns the index of the beginning of a block of cores in the coremap.
 */
static int
get_contiguous_cores(unsigned alloc_cores){
	KASSERT(spinlock_do_i_hold(&coremap_lock));

	for(unsigned int i = 0; i < corecount; i++){
		// If the page is free, see if alloc_cores more are
		if(coremap[i].state == COREMAP_FREE){
			unsigned int audit = 1;
			for(unsigned int j = 1; j < alloc_cores; j++){
				if(coremap[i+j].state == COREMAP_FREE){
					audit++;
				}else{
					break;
				}	
			} // Coremap lookahead loop //
			
			// Free contiguous block has been found!
			if( audit == alloc_cores ){
				return i;
			}
		}
	} // Coremap iterator loop //

	// At least the first coremap page is guaranteed to be fixed. 0 is invalid then.
	return 0;
}

/* Page allocator. Returns the physical address of the beginning of the block of pages. */
paddr_t
alloc_ppages(unsigned npages){
	paddr_t allocation;

	spinlock_acquire(&coremap_lock);

	// If you try and allocate more pages than available, you're going to have a bad time...
	if(total_page_allocs >= corecount){
		spinlock_release(&coremap_lock);
		return 0;
	}

	if(!stay_strapped){				// VM Hasn't Bootstrapped
		panic("Fatal: KMALLOC before VM has bootstrapped.");
		return 0;
	}else if(npages <= 1){				// Allocate a single core
		for(unsigned int n = 0; n < corecount; n++){
			if(coremap[n].state == COREMAP_FREE){
				allocation = coremap[n].paddr;
				coremap[n].state = COREMAP_DIRTY;
				coremap[n].istail = true;
				break;
			}
		}
	}else{						// Allocate npages contiguous cores
		unsigned int offset;
		// Get a paddr to the beginning of a block of cores
		offset = get_contiguous_cores(npages);
		if(offset == 0){
			spinlock_release(&coremap_lock);
			return ENOMEM;
			// Contiguous block couldn't be found. Fragmented or full!
		}		
		allocation = coremap[offset].paddr;		

		// Update the parameters in each core of the allocation
		for(unsigned int i = offset; i < (offset+npages); i++){
			coremap[i].state = COREMAP_DIRTY;

			if( (offset+npages)-i == 1 ){
				coremap[i].istail = true;
			}
		}
	}

	/* It's important to fill the new page with zeros or else data from
	 * a previous deallocation could, and probably does exist, in
	 * the page.
 	 */
	bzero((void *)PADDR_TO_KVADDR(allocation), PAGE_SIZE);

	total_page_allocs += npages;
	spinlock_release(&coremap_lock);

	return allocation;
}

/* Wrapper to convert physical address from alloc_ppages to kernel virtual addresses for kmalloc */
vaddr_t
alloc_kpages(unsigned npages){
	
	paddr_t allocation;

	allocation = alloc_ppages(npages);
	if(allocation == 0){
		return 0;
	}
	return PADDR_TO_KVADDR(allocation);
}

/* Free a certain number of cores */
/* Steps to completion:
 * (1) Acquire spinlock.
 * (2) Find the vaddr that matches the passed argument.
 * (3) Change internal values of all cores, up to and including the tail core.
 * (4) Release spinlock and return.
 */
void
free_kpages(vaddr_t addr){

	spinlock_acquire(&coremap_lock);
	for(unsigned int i = 0; i < corecount; i++){
		if(coremap[i].vaddr == addr){
			// Found matching vaddr, free all cores from addr to and include the tail
			// page.
			int incr = 0;
			while(coremap[i+incr].istail == false){
				coremap[i+incr].state = COREMAP_FREE;
				incr++;
				total_page_allocs--;
			}

			coremap[i+incr].state = COREMAP_FREE;
			coremap[i+incr].istail = false;
			total_page_allocs--;
	
			
			break;
		}	
	}

	spinlock_release(&coremap_lock);
	
	return;
}

/* Override function to correctly free a single coremap page. It's used by sbrk() to 
 * free physical pages in the heap, as well as as_destroy() to correctly free pages 
 * on an addrspace exit.
 */
void
free_ppage(paddr_t addr){
	if( addr == 0 ){
		return;
	}
	spinlock_acquire(&coremap_lock);
	
	for(unsigned int i = 0; i < corecount; i++){
		if(coremap[i].paddr == addr){
			if(coremap[i].istail == false){
				panic("Tried to free a physical page that's part of a set.\n");
			}else{
				coremap[i].state = COREMAP_FREE;
				total_page_allocs--;
			}
			break;
		}
	}

	spinlock_release(&coremap_lock);
	
	return;
}

/* Return the amount (in bytes) of memory that
 * allocated cores have taken up.
 */
unsigned int
coremap_used_bytes(){
	// Includes bytes taken by coremap
	return total_page_allocs * PAGE_SIZE;
}

/****************************************************/

// Chop virtual and physical addresses to VPN/PPN
inline unsigned int
paddr_to_ppn(paddr_t paddr){
	//return paddr>>3;
	return paddr;
}

inline unsigned int
vaddr_to_vpn(vaddr_t vaddr){
	//return vaddr>>3;
	return vaddr;
}

// Invalidate all TLB entries. Used in as_activate()
void
vm_tlbshootdown_all(void){	
	
	int disable = splhigh();
	for(int i = 0; i < NUM_TLB; i++){
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	
	splx(disable);
	return;
}

// Invalidate a single TLB entry. Used by sbrk(-).
void
vm_tlbshootdown(const struct tlbshootdown *ts){
	(void)ts;

	return;
}

/* The user tried to access an address that isn't already in the TLB.
 * A page fault occurs when the page that the memory address belongs to
 * isn't allocated or isn't in main memory.
 *
 * Note: Don't kprintf in this method. Just don't do it...
 *
 * (1) Ensure the fault address lies in a valid segment. This could be
 * a region from as_define region, the stack, or heap.
 * (2) Align the fault address to determine what page we want.
 * (3) Find the pentry it belongs to, if it's paddr is 0, this is the
 * first access at that address and we need to allocate a page. (On-Demand Paging)
 * (4) TURN OFF INTERRUPTS!
 * (5) Load the entry to the TLB and reenable interrupts.
 */
int vm_fault(int faulttype, vaddr_t faultaddress){
	
	struct addrspace *addrsp;	

	// NULL pointer
	if( faultaddress <= (vaddr_t)10 ){
		//kprintf("Null pointer exception. This is fatal.\n");
		return EFAULT;
	}

	// Check the fault type.
	switch(faulttype){
		case VM_FAULT_READONLY:
			// Danger: Insufficient access permissions.
			kprintf("VM_FAULT_READONLY at 0x%x\n", faultaddress);
			return EFAULT;
		
		case VM_FAULT_READ:
			break;

		case VM_FAULT_WRITE:
			break;

		default:
			return EINVAL;
	}
	
	// Ensure we're in a valid user process & address space is set up.
	if( curproc == NULL ){
		return EFAULT;
	}
	addrsp = proc_getas();
	if( addrsp == NULL ){
		return EFAULT;
	}
	
	bool is_valid_faultaddr = false;
	struct area *useg;
	struct pentry *load_page;
	/* Check to see if the fault address is valid. We need to check:
	 * (1) Segments
	 * (2) Stack
	 * (3) Heap
	 */
	if( faultaddress >= (USERSTACK-(ADDRSP_STACKSIZE*PAGE_SIZE)) && faultaddress < USERSTACK ){
		is_valid_faultaddr = true; //(2)
		load_page = addrsp->stack;
	}else if( faultaddress >= addrsp->as_heap_start && faultaddress <= addrsp->as_heap_end ){
		is_valid_faultaddr = true; //(3)
		load_page = addrsp->heap;
	}else{
		useg = addrsp->segments;
		while( useg != NULL ){
			if( faultaddress >= useg->vstart && faultaddress < (useg->vstart + useg->bytesize) ){
				is_valid_faultaddr = true; //(1)
				load_page = useg->pages;
				break;
			}
			useg = useg->next;
		}
	}

	// Check yoself b4 u rek yoself
	if(!is_valid_faultaddr){
		//panic("Tried to access an invalid memory region: 0x%x.\n", faultaddress);
		return EFAULT;
	}


	bool page_fault = true;
	faultaddress &= PAGE_FRAME;	
	
	// Walk the segment's page table to see if the page is allocated already
	while( load_page != NULL ){
		if( load_page->vaddr == vaddr_to_vpn(faultaddress) ){
			if(load_page->paddr != 0){
				page_fault = false;
			}	
			break;
		}
		load_page = load_page->next;
	}

	if(load_page == NULL){
		//panic("Couldn't find a page searching for 0x%x\n", faultaddress);
		return EFAULT;
	}

	// If the physical page isn't assigned, we need to allocate one
	if(page_fault){
		paddr_t ppage = alloc_ppages(1);
		load_page->paddr = paddr_to_ppn(ppage);		
	}
	
	// Finally, update the TLB with the new physical page
	uint32_t ehi = faultaddress;
	uint32_t elo = load_page->paddr | TLBLO_DIRTY | TLBLO_VALID;


	int off = splhigh();
	int index = tlb_probe(ehi, 0);
	
	if(index > 0){
		tlb_write(ehi, elo, index);
	}else{
		tlb_random(ehi, elo);
	}

	splx(off);

	return 0;
}

/****************************************************************************/
/* This VM syscall moves the heap breakpoint up and down. It's used in malloc, and
 * is of critical importance in dynamic memory de/allocations. We need to do accomplish
 * different tasks depending on the argument:
 * (i) Argument is 0. Useless syscall at that point, just return the heap_end.
 * (ii) Argument is positive. Ensure it can %4; allocate more pages and extend heap_end
 * (iii) Argument is negative. Ensure it can %4; free pages and retract heap_end
 *
 */
int
sys_sbrk(int shift, int *retval){
	struct addrspace *addrsp;
	addrsp = proc_getas();
	if( addrsp == NULL ){
		return EFAULT;
	}	

// LOCK_ACQUIRE
	if( shift == 0 ){
		*retval = addrsp->as_heap_end;
		return 0;
	}else if( (shift%4) != 0 ){
		// Pointer-align the number of bytes
		//shift += (4 - (shift%4) );
		return EINVAL;
	}	

	// Ensure our address space has been properly prepared for a heap
	KASSERT(addrsp->as_heap_start != 0 && addrsp->as_heap_end != 0);
	
	// Find the number of pages needed, rounded up. This value can be negative.
	int num_pages = shift / PAGE_SIZE;
	if( (shift%PAGE_SIZE) != 0 ){
		num_pages++;
	}
	
	// Check which way to move the heap breakpoint
	if( shift > 0 ){
		KASSERT(num_pages > 0);
		// Increase heap size
		// Check to make sure we don't collide with stack.
		if( (addrsp->as_heap_end + shift) > (USERSTACK - (ADDRSP_STACKSIZE * PAGE_SIZE)) ){
			*retval = -1;
			return ENOMEM;
		}

		*retval = addrsp->as_heap_end;
	
		// Actually allocate the pages
		for(int i = 0; i < num_pages; i++){
			struct pentry *entry;
			entry = kmalloc(sizeof(*entry));
			if(entry == NULL){
				return ENOMEM;
			}
		
			entry->vaddr = addrsp->as_heap_end;
			entry->paddr = alloc_ppages(1);
			entry->next = NULL;

			if(entry->paddr == 0){
				kfree(entry);
				return ENOMEM;
			}

			if(addrsp->heap == NULL){
				addrsp->heap = entry;
			}else{
				struct pentry *tail;
				tail = addrsp->heap;

				while(tail->next != NULL){
					tail = tail->next;
				}

				tail->next = entry;
			}

			addrsp->as_heap_end += PAGE_SIZE;
		}
	}else{					// Decrease heap size
		num_pages *= -1;
		KASSERT(num_pages > 0);
	
		int heapcheck = addrsp->as_heap_end + shift;
		// Ensure we're not completely deleting the heap.
		if( (addrsp->as_heap_end + shift) < addrsp->as_heap_start || heapcheck < 0 ){
			*retval = -1;
			return EINVAL;
		}

		/* Deallocate pages. These 2 lines are enough to pass the basic sbrk() tests,
		 * but we don't ACTUALLY free any pages, just move the breakpoint. */
		*retval = addrsp->as_heap_end;
		addrsp->as_heap_end += shift;

		/* Here we actually free coremap pages for later use. Now I'm suddenly
		 * regretting using forward-only linked lists...
		 */
		for(int j = 0; j < num_pages; j++){
			struct pentry *heapdel;		
			struct pentry *tail;	
			//struct tlbshootdown *bang;

			tail = addrsp->heap;
			if( tail->next == NULL ){		// Heap has 1 page
				free_ppage(tail->paddr);
				addrsp->heap = NULL;
				kfree(tail);
			}else{					// Heap has 2 or more pages
				while( tail->next->next != NULL ){
					tail = tail->next;
				}
				heapdel = tail->next;
				free_ppage(heapdel->paddr);
				tail->next = NULL;
				kfree(heapdel);
			}
		}
		//vm_tlbshootdown_all();	
	}	

// LOCK_RELEASE
	return 0;
}
