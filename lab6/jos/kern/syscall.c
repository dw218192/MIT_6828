/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/kmalloc.h>
#include <kern/time.h>
#include <kern/e1000.h>

#define debug 0

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.
	// LAB 3: Your code here.
	user_mem_assert(curenv, s, len, 0);

	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int
sys_cgetc(void)
{
	return cons_getc();
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	
	if(debug)
	{
		if (e == curenv)
			cprintf("[%08x] exiting gracefully\n", curenv->env_id);
		else
			cprintf("[%08x] destroying %08x\n", curenv->env_id, e->env_id);
	}

	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	struct Env* e;
	struct PageInfo* pinfo;
	int r;

	if((uintptr_t) va >= UTOP || (uintptr_t) va & (PGSIZE-1))
		return -E_INVAL;
	
	// if perm is inappropriate
	if(!(perm & PTE_U) || !(perm & PTE_P))
		return -E_INVAL;
	if(perm & ~PTE_SYSCALL)
		return -E_INVAL;

	//if environment envid doesn't currently exist, or the caller doesn't have permission to change envid.
	r = envid2env(envid, &e, 1);
	if(r < 0)
		return r;

	pinfo = page_alloc(ALLOC_ZERO);
	r = page_insert(e->env_pgdir, pinfo, va, perm);
	if(r < 0)
		return r;
	
	return 0;
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	int r;
	struct Env *e;

	r = env_alloc(&e, curenv->env_id);
	if(r < 0)
		return r;

	e->env_status = ENV_NOT_RUNNABLE;

	//copy register states
	e->env_tf = curenv->env_tf;

	//return 0 in child
	e->env_tf.tf_regs.reg_eax = 0;

	return e->env_id;
}

static snapshotid_t
sys_env_snapshot(envid_t envid)
{
	int i,r;
	struct Env *e;
	uintptr_t va;
	snapshotid_t ssid;
	struct Snapshot* ss;
	struct SavedPage* dummy, *cur, *temp;

	struct PageInfo* pinfo;
	pte_t* pte;
	physaddr_t pa;

	dummy = NULL;
	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	if ((r = snapshot_alloc(&ssid) < 0))
		return r;

	ss = id2snapshot(ssid);
	ss->envid = envid;
	
	dummy = kmalloc(sizeof(struct SavedPage));
	cur = dummy;

	// save address space
	for(va=UTEXT; va<UTOP; va+=PGSIZE)
	{
		if(page_lookup(e->env_pgdir, (void*)va, &pte))
		{
			cur->next = kmalloc(sizeof(struct SavedPage));

			cur = cur->next;
			cur->page_vm = va;
			cur->page_perm = PTE_FLAGS(*pte);
			cur->next = NULL;

			pa = PTE_ADDR(*pte);

			//save page
			pinfo = page_alloc(0);
			if(!pinfo)
				goto bad;
			
			cur->saved_page = pinfo;
			memcpy(page2kva(pinfo), (void*) KADDR(pa), PGSIZE);
		}
	}
	
	cur = dummy->next;
	kfree(dummy);

	ss->saved_pages = cur;

	// save register states
	ss->utf.utf_eflags = e->env_tf.tf_eflags;
	ss->utf.utf_eip = e->env_tf.tf_eip;
	ss->utf.utf_esp = e->env_tf.tf_esp;
	ss->utf.utf_regs = e->env_tf.tf_regs;
	ss->utf.utf_err = e->env_tf.tf_err;

	// not used in this case
	ss->utf.utf_fault_va = 0;

	return ssid;

bad:
	//store dummy so it will be dealloc'ed by snapshot_free
	ss->saved_pages = dummy;
	snapshot_free(ssid);

	return -E_NO_MEM;
}

// roll back the environment to a snapshot
static int
sys_env_resume(envid_t envid, snapshotid_t snapshotid)
{
	void _savedpages_free(struct SavedPage* cur);

	int i, r;
	struct Env *e;
	struct Snapshot *ss;
	struct SavedPage *cur, *new_page_start, *new_page;
	struct PageInfo *pinfo;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;

	ss = id2snapshot(snapshotid);
	if (!ss || ss->envid != envid)
	{
		return -E_INVAL;
	}

	// allocate all necessary pages and store them in a linked list before doing anything to the env
	// so that we don't end up with intermediate states
	cur = ss->saved_pages;
	new_page_start = kmalloc(sizeof(struct SavedPage));
	new_page = new_page_start;

	while(cur)
	{
		new_page->next = kmalloc(sizeof(struct SavedPage));
		new_page = new_page->next;

		new_page->saved_page = page_alloc(0);
		if(!new_page->saved_page)
		{
			new_page->next = NULL;
			_savedpages_free(new_page_start);
			return -E_NO_MEM;
		}
		
		memcpy(page2kva(new_page->saved_page), page2kva(cur->saved_page), PGSIZE);
		
		cur = cur->next;
	}
	
	//free address space
	env_flush_addr_space(e);

	cur = ss->saved_pages;
	new_page = new_page_start->next;
	
	while(cur)
	{
		if((r = page_insert(e->env_pgdir, new_page->saved_page, (void*) cur->page_vm, cur->page_perm)) < 0)
		{
			_savedpages_free(new_page_start);
			env_free(e);

			return r;
		}

		cur = cur->next;
		new_page = new_page->next;
	}

	//free dummy node
	kfree(new_page_start);

	e->env_tf.tf_eflags = ss->utf.utf_eflags;
	e->env_tf.tf_esp = ss->utf.utf_esp;
	e->env_tf.tf_eip = ss->utf.utf_eip;
	e->env_tf.tf_regs = ss->utf.utf_regs;

	e->env_status = e == curenv ? ENV_RUNNING : ENV_RUNNABLE;
	return 0;

bad:
	_savedpages_free(new_page_start);

	return -E_NO_MEM;
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	int r;
	struct Env *e;

	if(status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE)
		return -E_INVAL;

	r = envid2env(envid, &e, 1);
	if(r < 0)
		return r;
	
	e->env_status = status;

	return 0;
}

// Set envid's trap frame to 'tf'.
// tf is modified to make sure that user environments always run at code
// protection level 3 (CPL 3), interrupts enabled, and IOPL of 0.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
	// LAB 5: Your code here.
	// Remember to check whether the user has supplied us with a good
	// address!
	int r;
	struct Env *e;

	r = envid2env(envid, &e, 1);
	if(r < 0)
		return r;

	e->env_tf.tf_ds = GD_UD | 3;
	e->env_tf.tf_es = GD_UD | 3;
	e->env_tf.tf_ss = GD_UD | 3;
	e->env_tf.tf_cs = GD_UT | 3;
	e->env_tf.tf_eflags &= ~FL_IOPL_MASK;
	e->env_tf.tf_eflags |= FL_IF;
	e->env_tf = *tf;

	return 0;

	// panic("sys_env_set_trapframe not implemented");
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.

int
sys_page_map(envid_t src_envid, void *src_pg,
		     envid_t dst_envid, void *dst_pg, int perm)
{
	int r;
	struct Env* src_e, *dst_e;
	pte_t* src_pte;
	struct PageInfo* pginfo;
	
	if((uintptr_t)src_pg >= UTOP || 
		(uintptr_t)dst_pg >= UTOP || 
		(uintptr_t)src_pg & (PGSIZE-1) || 
		(uintptr_t)dst_pg & (PGSIZE-1))
		return -E_INVAL;
	
	// if perm is inappropriate
	if(!(perm & PTE_U) || !(perm & PTE_P))
		return -E_INVAL;
	if(perm & ~PTE_SYSCALL)
		return -E_INVAL;

	// if srcenvid and/or dstenvid doesn't currently exist, or the caller doesn't have permission to change one of them.
	r = envid2env(src_envid, &src_e, 1);
	if(r < 0)
		return r;
	r = envid2env(dst_envid, &dst_e, 1);
	if(r < 0)
		return r;
	
	pginfo = page_lookup(src_e->env_pgdir, src_pg, &src_pte);
	if(!pginfo)
		return -E_INVAL;
	
	if(perm & PTE_W && !(*src_pte & PTE_W))
		return -E_INVAL;
	
	r = page_insert(dst_e->env_pgdir, pginfo, dst_pg, perm);
	if(r < 0)
		return r;

	return 0;
}


// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
int	sys_page_unmap(envid_t envid, void *pg)
{
	int r;
	struct Env *e;
	struct PageInfo* pginfo;

	if((uintptr_t) pg >= UTOP || (uintptr_t) pg & (PGSIZE-1))
		return -E_INVAL;

	r = envid2env(envid, &e, 1);
	if(r < 0)
		return r;

	page_remove(e->env_pgdir, pg);
	return 0;
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	int r;
	struct Env* e;

	r = envid2env(envid, &e, 1);
	if(r < 0)
		return r;
	
	e->env_pgfault_upcall = func;
	return 0;
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
	if ((uintptr_t)dstva < UTOP && ((uintptr_t)dstva & (PGSIZE-1)))
		return -E_INVAL;

	curenv->env_status = ENV_NOT_RUNNABLE;
	curenv->env_ipc_dstva = dstva;
	curenv->env_ipc_value = 0;
	curenv->env_ipc_perm = 0;
	curenv->env_ipc_from = 0;
	curenv->env_ipc_recving = 1;

	curenv->env_tf.tf_regs.reg_eax = 0;
	sched_yield();

	return 0;
}

// Try to send 'value' to the target env 'envid'.
// If va != 0, then also send page currently mapped at 'va',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target has not requested IPC with sys_ipc_recv.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused ipc_recv system call.
//
// If the sender sends a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc doesn't happen unless no errors occur.
//
// Returns 0 on success where no page mapping occurs,
// 1 on success where a page mapping occurs, and < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	struct Env* e;
	int r, ret;
	struct PageInfo* pinfo;
	pte_t *pte;

	if((r = envid2env(envid, &e, 0)) < 0)
		return r;

	if(!e->env_ipc_recving)
		return -E_IPC_NOT_RECV;

	if((uintptr_t)srcva < UTOP)
	{
		if((uintptr_t)srcva & (PGSIZE-1))
			return -E_INVAL;
		
		// if perm is inappropriate
		if(!(perm & PTE_U) || !(perm & PTE_P))
			return -E_INVAL;

		if(perm & ~PTE_SYSCALL)
			return -E_INVAL;

		if(!(pinfo = page_lookup(curenv->env_pgdir, srcva, &pte)))
			return -E_INVAL;

		if((perm & PTE_W) && !(*pte & PTE_W))
			return -E_INVAL;

		if((r = page_insert(e->env_pgdir, pinfo, e->env_ipc_dstva, perm)) < 0)
			return r;
		
		e->env_ipc_perm = perm;
		ret = 1;
	}
	else
	{
		e->env_ipc_perm = 0;
		ret = 0;
	}

	
	e->env_ipc_value = value;
	e->env_ipc_from = curenv->env_id;
	e->env_ipc_recving = 0;

	e->env_status = ENV_RUNNABLE;

	return ret;
}

// Return the current time.
static int
sys_time_msec(void)
{
	// LAB 6: Your code here.
	// panic("sys_time_msec not implemented");

	return time_msec();
}

// Transmit a packet to network in user space
//
// Return 0 on success, < 0 on error.  Errors are:
//  -E_TX_FULL if the transmission queue is full
//  -E_PKT_TOO_LONG if the packet size exceeds limit
static int
sys_net_transmit(const void* data, uint16_t len)
{
	user_mem_assert(curenv, data, len, 0);

	return e1000_transmit(data, len);
}

// Receive a packet from network in user space
//
// Return the size of the data written on success, < 0 on error.  Errors are:
//  -E_RX_EMPTY if no packet is received.
static int
sys_net_recv(void* buf)
{
	static char recv_tmp_buf[E1000_RBS];
	int r;
	r = e1000_receive(recv_tmp_buf);
	
	if(r > 0)
	{
		user_mem_assert(curenv, buf, r, 0);

		if(rcr3() != PADDR(curenv->env_pgdir))
			lcr3(PADDR(curenv->env_pgdir));
		
		memcpy(buf, recv_tmp_buf, r);
	}

	return r;
}
// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	// LAB 3: Your code here.

	// panic("syscall not implemented");

	switch (syscallno) {
		case SYS_cputs:
			sys_cputs((const char*) a1, a2);
			return 0;
		case SYS_cgetc:
			return sys_cgetc();
		case SYS_getenvid:
			return sys_getenvid();
		case SYS_env_destroy:
			return sys_env_destroy((envid_t)a1);
		case SYS_page_alloc:
			return sys_page_alloc((envid_t)a1, (void*)a2, (int)a3);
		case SYS_env_set_trapframe:
			return sys_env_set_trapframe((envid_t)a1, (struct Trapframe*)a2);
		case SYS_page_map:
			return sys_page_map((envid_t)a1, (void*)a2, (envid_t)a3, (void*)a4, (int)a5);
		case SYS_page_unmap:
			return sys_page_unmap((envid_t)a1, (void*)a2);
		case SYS_exofork:
			return sys_exofork();
		case SYS_env_snapshot:
			return sys_env_snapshot((envid_t)a1);
		case SYS_env_resume:
			return sys_env_resume((envid_t)a1, (snapshotid_t)a2);
		case SYS_env_set_status:
			return sys_env_set_status((envid_t)a1, (int)a2);
		case SYS_env_set_pgfault_upcall:
			return sys_env_set_pgfault_upcall((envid_t)a1, (void*)a2);
		case SYS_yield:
			sys_yield();
			return 0;
		case SYS_ipc_try_send:
			return sys_ipc_try_send((envid_t)a1, a2, (void*)a3, a4);
		case SYS_ipc_recv:
			return sys_ipc_recv((void*)a1);
		case SYS_time_msec:
			return sys_time_msec();
		case SYS_net_transmit:
			return sys_net_transmit((const void*)a1, (uint16_t)a2);
		case SYS_net_recv:
			return sys_net_recv((void*)a1);
		default:
			return -E_INVAL;
	}
}

