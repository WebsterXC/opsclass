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

static unsigned long corecount;		// Number of total cores
static bool stay_strapped = false;	// Has vm_bootstrap run yet?

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
	paddr_t first;
	paddr_t last;
	
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
	KASSERT(num_cores != 0);
	corecount = num_cores;
	map_size = num_cores * sizeof(struct core);

	// Reserve memory for the coremap (firstpaddr altered)
	// paddr_coremap = ram_stealmem( DIVROUNDUP(map_size, PAGE_SIZE) );
	
	// Convert paddr to a kernel virtual address
	coremap = (struct core *)PADDR_TO_KVADDR(first);

	// Set coremap cores to fixed state, others to dirty
	for(unsigned int i = 0; i < num_cores; i++){
		// If coremap lies on this core, it's fixed in place
		if( i < DIVROUNDUP(map_size, PAGE_SIZE) ){
			coremap[i].state = COREMAP_FIXED;	
		}else{	
			coremap[i].state = COREMAP_DIRTY;
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

vaddr_t
alloc_kpages(unsigned npages){
	(void)npages;
	//paddr_t allocation;

	spinlock_acquire(&coremap_lock);

	spinlock_release(&coremap_lock);

	return (vaddr_t)0;
}

/* Free a certain number of pages */
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


	spinlock_release(&coremap_lock);
	
	return;
}

/* Return the amount (in bytes) of memory that
 * allocated cores have taken up.
 */
unsigned int
coremap_used_bytes(){
	unsigned int used = 0;
	for(unsigned int i = 0; i < corecount; i++){
		if(coremap[i].state < 3){
			used++;
		}
	}

	return used * PAGE_SIZE;
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

struct addrspace *
as_create(void){
	

	return NULL;
}

void
as_destroy(struct addrspace *addrsp){
	(void)addrsp;

	return;
}

void 
as_activate(void){

	return;
}

void
as_deactivate(void){

	return;
}

int
as_define_region(struct addrspace *addrsp, vaddr_t vaddr, size_t sz,
			int read, int write, int exec){
	(void)addrsp;
	(void)vaddr;

	return sz + read + write + exec;
}

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

int
as_copy(struct addrspace *old, struct addrspace **new){
	(void)old;
	(void)new;

	return 0;
}

/////////////////////////////////////////////////////////////////
