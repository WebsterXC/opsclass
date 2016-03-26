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

extern vaddr_t firstfree;

static unsigned long corecount;		// Number of total cores
static bool stay_strapped = false;	// Has vm_bootstrap run yet?

static volatile unsigned int total_page_allocs = 0;
static struct core *coremap;
/* Virtual Memory Wizardry Here */
//////////////////////////////////

/* Here we need to create the coremap to store physical page info.
 * This requires a few steps:
 * (1) Setup coremap resources (e.g. spinlock)
 * (2) Find available RAM based on first and last addresses of available memory.
 * (3) Get total number of pages. Available RAM (last-first)/page size (4K).
 * (4) Manually allocate the coremap, get it's starting paddr. Assign to PADDR_TO_KVADDR
 * (5) Iterate through coremap and initialize core information.
 * (6) Set up global coremap lock.
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

	// Reserve memory for a coremap lock. Should only need 1 page.
	/***** Static initializer?? *****/
	//paddr_t lockmem;
	//lockmem = ram_stealmem( DIVROUNDUP(sizeof(*coremap_lock), PAGE_SIZE) );
	//if(lockmem == 0){
	//	panic("VM Bootstrapping Failed!");
	//}
	//coremap_lock = PADDR_TO_KVADDR(lockmem);

	// Get last and first address of RAM.
	// *** ram_stealemem will no longer work ***
	last = ram_getsize();
	first = ram_getfirstfree(); 

	// Get number of available cores, and size of coremap (in bytes)
	num_cores = (last - first) / PAGE_SIZE;
	//unsigned int mem;
	//mem = mainbus_ramsize() - (uint32_t)(firstfree - MIPS_KSEG0);
	//num_cores = (mem + PAGE_SIZE-1) / PAGE_SIZE;
	kprintf("Num cores: %d\n", num_cores);	

	KASSERT(num_cores != 0);
	corecount = num_cores;
	map_size = num_cores * sizeof(struct core);

	// Reserve memory for the coremap (firstpaddr altered)
	// paddr_coremap = ram_stealmem( DIVROUNDUP(map_size, PAGE_SIZE) );
	
	// Convert paddr to a kernel virtual address
	coremap = (struct core *)PADDR_TO_KVADDR(first);

	// Set coremap cores to fixed state, others to free
	for(unsigned int i = 0; i < num_cores; i++){
		// If coremap lies on this core, it's fixed in place
		if( i < DIVROUNDUP(map_size, PAGE_SIZE) ){
			coremap[i].state = COREMAP_FIXED;	
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

		//Manually increment by bytes to next page
		first += PAGE_SIZE;
	}
		
	// Paging initialized. Kmalloc should work now.
	//spinlock_init(coremap_lock);

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
	(void)npages;
	paddr_t allocation;

	spinlock_acquire(&coremap_lock);

	if(total_page_allocs*PAGE_SIZE > mainbus_ramsize()-(uint32_t)(firstfree - MIPS_KSEG0)){
		spinlock_release(&coremap_lock);
		return 0;
	};

	if(!stay_strapped){				// VM Hasn't Bootstrapped
		allocation = ram_stealmem(npages);
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
			// Swap out cores?
			// Contiguous block couldn't be found. Fragmented!
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

/* Wrapper to convert physical address from alloc_ppages to kernel virtual addresses */
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
	//unsigned int used = 0;
	
	//spinlock_acquire(&coremap_lock);	

	//for(unsigned int i = 0; i < corecount; i++){
	//	if(coremap[i].state != COREMAP_FREE){
	//		used++;
	//	}
	//}
	
	//spinlock_release(&coremap_lock);	

	return total_page_allocs * PAGE_SIZE;
}

void
vm_tlbshootdown_all(void){	

	return;
}

void
vm_tlbshootdown(const struct tlbshootdown *ts){
	(void)ts;

	return;
}

int vm_fault(int faulttype, vaddr_t faultaddress){
	(void)faulttype;
	(void)faultaddress;
	

	return 0;
}

/* Address Space Shenanigans Here */
////////////////////////////////////

/* To create an address space, we need to:
 * (1) Allocate an addrspace using kmalloc
 * (2) Initialize struct variables
 *
 * (Note) Regions are first defined in as_define_region
 *
 */

/*
struct addrspace *
as_create(void){
	struct addrspace *addrsp;

	// Allocate memory for addrspace	
	addrsp = kmalloc(sizeof(*addrsp));
	if(addrsp == NULL){
		return NULL;
	}
	

	return NULL;
}

void
as_destroy(struct addrspace *addrsp){
	(void)addrsp;

	return;
}

void 
as_activate(void){

	// Shoot down all TLB entries

	return;
}

void
as_deactivate(void){

	return;
}
*/
/* This is an important part of defining an addrspace. 
 * This function allocates regions of memory in the address
 * space including stack, heap, and static code areas.
 */

/* We need to:
 * (1) Initialize a new area struct for the segment.
 * (2) Reserve pages for the address space to reside in.
 * (3) Update internal information, like permissions.
 * (4) Add the new area to the linked list.
 *
 *
 *
 */
/*
int
as_define_region(struct addrspace *addrsp, vaddr_t vaddr, size_t sz,
			int read, int write, int exec){
	(void)addrsp;

	unsigned int page_count;

	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;
	page_count = sz / PAGE_SIZE;


	return read + write + exec;
}
*/
/* Using the segment information generated in as_define_region,  */
/* Steps:
 * (1) Set up our page table based on region information above.
 * (2) Generate a pentry for each page needed and add to linked list.
 *
 *
 *
 */
/*
int
as_prepare_load(struct addrspace *addrsp){
	(void)addrsp;


	return 0;
}

int
as_complete_load(struct addrspace *addrsp){
	(void)addrsp;
	
	return 0;
}

int
as_define_stack(struct addrspace *addrsp, vaddr_t *stackptr){
	(void)addrsp;
	(void)stackptr;

	return 0;
}
*/
/* Copy an address space */
/*
 *
 *
 *
 *
 *
 *
 */
/*
int
as_copy(struct addrspace *old, struct addrspace **new){
	(void)old;
	(void)new;

	return 0;
}
*/
/////////////////////////////////////////////////////////////////
