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
static struct spinlock tlb_lock = SPINLOCK_INITIALIZER;		// Synchro primitive for tlb access

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
		
	// Paging initialized. Kmalloc should work now.

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
	(void)addr;

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

/* Return the amount (in bytes) of memory that
 * allocated cores have taken up.
 */
unsigned int
coremap_used_bytes(){
	// Includes bytes taken by coremap
	return total_page_allocs * PAGE_SIZE;
}

/* Fully-Automatic Fun. Bang Bang Bang! */
void
vm_tlbshootdown_all(void){	

	return;
}

/* Semi-Automatic, Bang! */
void
vm_tlbshootdown(const struct tlbshootdown *ts){
	(void)ts;

	return;
}

/* This occurs when there is a page fault: a user process tried to access
 * a coremap page that is not allocated or not in memory. We need to:
 * (1) Ensure the fault address is a valid address.
 * (1) Determine the fault type. It could be: VM_FAULT_READONLY, VM FAULT_READ
 * 	or VM_FAULT_WRITE.
 * (2)  READ/WRITE: Insert the page to the TLB.
 * (3)  READONLY: Invalid access. Return nonzero.
 */
int vm_fault(int faulttype, vaddr_t faultaddress){

	unsigned int offset;	
	struct addrspace *addrsp;	

	// Check the fault type
	switch(faulttype){
		case VM_FAULT_READONLY:
			// Danger: Insufficient access permissions.
			kprintf("VM_FAULT_READONLY at 0x%x\n", faultaddress);
			return EFAULT;
		
		case VM_FAULT_READ:
			kprintf("VM_FAULT_READ at 0x%x\n", faultaddress);
			break;

		case VM_FAULT_WRITE:
			kprintf("VM_FAULT_WRITE at 0x%x\n", faultaddress);
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

	// If any of these are true the address space is broken.
	KASSERT(addrsp->pages != NULL);
	KASSERT(addrsp->segments != NULL);
	KASSERT(addrsp->as_heap_start != 0 && addrsp->as_heap_start != 1);
	KASSERT(addrsp->as_heap_end != 0 && addrsp->as_heap_end != 1);
	KASSERT(addrsp->as_stackpbase != 0 && addrsp->as_stackpbase != 1);
	KASSERT(addrsp->as_heappbase != 0 && addrsp->as_heappbase != 1);

	/* Here we traverse our segment list generated in as_define_region.
	 * Valid addresses are contained in our regions for the address space,
	 * but if we access an invalid location, return nonzero.
	 */
	struct area *valid_segments;
	bool is_valid_vaddr = false;

	valid_segments = addrsp->segments;
	while(valid_segments != NULL){
		if( faultaddress >= valid_segments->vstart && faultaddress < (valid_segments->vstart + valid_segments->bytesize) ){
			is_valid_vaddr = true;
			offset = (faultaddress - valid_segments->vstart) + valid_segments->pstart;
			break;
		}

		valid_segments = valid_segments->next;
	}	
	/* We also need to check the stack and heap addresses */
	vaddr_t stackbase = USERSTACK - (ADDRSP_STACKSIZE * PAGE_SIZE);
	if( faultaddress >= stackbase && faultaddress < USERSTACK ){
		is_valid_vaddr = true;
		offset = (faultaddress - stackbase) + addrsp->as_stackpbase;
	}else if( faultaddress >= addrsp->as_heap_start && faultaddress < addrsp->as_heap_end){
		is_valid_vaddr = true;
		offset = (faultaddress - addrsp->as_heap_start) + addrsp->as_heappbase;
	}

	// Invalid address
	if(!is_valid_vaddr){
		kprintf("Invalid Address.\n");
		return EFAULT;
	}

	/* Valid address. Now we need to insert the translation to the TLB.
	 * To calculate the offset, we first need to find the segment that the
	 * faulting address belongs to, which we did above. Then, walk the page
	 * table to see if any of the page->paddr's match the offset. If so, we
	 * insert the translation to the TLB.
	 */

	struct pentry *pt;
	pt = addrsp->pages;
	while(pt != NULL){
		// Page found, load to TLB
		if(pt->paddr == offset){
			spinlock_acquire(&tlb_lock);
			uint32_t ehi = faultaddress;
			uint32_t elo = offset | TLBLO_DIRTY | TLBLO_VALID;
			
			tlb_random(ehi, elo);

			spinlock_release(&tlb_lock);
			return 0;
		}		

		pt = pt->next;
	}
	// Page not found. Since we're not swapping, allocate a new page and add 
	// it to the addrspace's page table.
	// TODO

	/* I'm confused when the faulting address is not exactly the page's physical
	 * address. If the fault address is confirmed located in a segment, but lies inside of 
	 * a page (as opposed to the front of it), how do we map to a physical address?
	 */
	
	kprintf("Unable to locate 0x%x, but I have offset 0x%x\n", faultaddress, offset);

	struct pentry *trav;
	trav = addrsp->pages;
	while(trav != NULL){
		kprintf("0x%x\n", trav->paddr);
		trav = trav->next;
	}

	// Return nonzero for DEBUGGING ONLY
	return EFAULT;
	//return 0;
}
