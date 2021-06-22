// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	envid_t myid;
	void *tmp_addr = (void*) PFTEMP;
	pte_t pte = uvpt[PGNUM(addr)];
	myid = sys_getenvid();

	if(!(err & FEC_WR) || !(pte & PTE_COW))
	{
		panic("pgfault: va=%08x, err=%x\n", addr, err);
	}

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	if((r = sys_page_alloc(myid, tmp_addr, PTE_P | PTE_U | PTE_W) < 0))
		panic("pgfault: sys_page_alloc: %e\n", r);

	memmove(tmp_addr, ROUNDDOWN(addr, PGSIZE), PGSIZE);

	if((r = sys_page_map(myid, tmp_addr, myid, ROUNDDOWN(addr, PGSIZE), PTE_P | PTE_U | PTE_W)) < 0)
		panic("pgfault: sys_page_map: %e\n", r);

	if((r = sys_page_unmap(myid, tmp_addr)) < 0)
		panic("pgfault: sys_page_unmap: %e\n", r);

	// panic("pgfault not implemented");
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
	uintptr_t va = pn * PGSIZE;
	pte_t pte;
	uint32_t perm;

	// LAB 4: Your code here.
	pte = uvpt[pn];
	perm = PTE_FLAGS(pte);
	
	if ((pte & PTE_W) || (pte & PTE_COW))
	{
		// map child COW
		if ((r = sys_page_map(0, (void*)va, envid, (void*)va, PTE_P | PTE_U | PTE_COW)) < 0)
			return r;

		// map myself COW
		if ((r = sys_page_map(0, (void*)va, 0, (void*)va, PTE_P | PTE_U | PTE_COW)) < 0)
			return r;
	}
	else
	{
		// only necessary to map child
		if ((r = sys_page_map(0, (void*)va, envid, (void*)va, perm)) < 0)
			return r;
	}

	// panic("duppage not implemented");
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	envid_t id;
	uintptr_t addr;
	int r;
	// linker symbol that marks the end of the env's memory
	extern unsigned char end[];

	// set page fault handler
	set_pgfault_handler(pgfault);

	if ((id = sys_exofork()) < 0)
		return id;
	
	// child finishes here
	if (id == 0)
	{
		thisenv = &envs[ENVX(sys_getenvid())];
		return id;
	}

	// parent sets up child's address space
	for (addr = UTEXT; addr < (uintptr_t)end; addr += PGSIZE)
	{
		if((uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P))
		{
			if((r = duppage(id, PGNUM(addr))) < 0)
				return r;
		}
	}

	// COW the stack as well
	if((r = duppage(id, PGNUM(USTACKTOP - PGSIZE))) < 0)
		return r;

	// allocate an exception stack for the child
	if((r = sys_page_alloc(id, (void*)(UXSTACKTOP-PGSIZE), PTE_P | PTE_U | PTE_W)) < 0)
		return r;

	// register the page fault handler for the child
	extern void _pgfault_upcall(void);
	if((r = sys_env_set_pgfault_upcall(id, _pgfault_upcall)) < 0);
		return r;

	// Start the child environment running
	if ((r = sys_env_set_status(id, ENV_RUNNABLE)) < 0)
		return r;

	return id;

	// LAB 4: Your code here.
	// panic("fork not implemented");
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
