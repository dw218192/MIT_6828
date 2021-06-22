## **Lab4: Preemptive Multitasking**

### **Part A: Multiprocessor Support and Cooperative Multitasking**
### **Part A-I. the LAPIC module**
  * APIC stands for _advanced programmable interrupt controller_
  * in an SMP system, each CPU has an accompanying local APIC (LAPIC) unit
  * The LAPIC provides various functionalities such as
    * allowing BSP to send a start-up signal to its connected CPU
    * handling cpu-specific interrupt configuration
    * providing its connected CPU with a unique identifier
  * the CPU accesses its LAPIC through memory-mapped IO
    * LAPIC registers are mapped to a hole in physical memory starting at `0xFEE000000`
    * the address `0xFEE000000` is the same for all processors (which means you can only get info about the CPU the code is running on)



---

### **Exercise 1: set up the kernel page table to be able to access the LAPIC hole using VM**
**`pmap.c`**
```c
void* mmio_map_region(physaddr_t lapicaddr, size_t size)
{	
	static uintptr_t base = MMIOBASE;

	physaddr_t start = ROUNDDOWN(lapicaddr, PGSIZE);
	physaddr_t end = ROUNDUP(lapicaddr + size, PGSIZE);	
	
	size = end-start;

	if(base+size > MMIOLIM)
		panic("mmio_map_region: MMIO mem overflow");

	/*
		IA32-3A: For correct APIC operation, this address space must be mapped to an area 
		of memory that has been designated as strong uncacheable (UC). 
	*/
	boot_map_region(kern_pgdir, base, size, start, PTE_W | PTE_PCD | PTE_PWT);
	base += size;
	return (void*) (base - size);
}
```

NOTE: I added a check to make sure mmio_map_region is run only once.\
This is because `lapicaddr` is the same for all processors.\
It doesn't make sense to map multiple pages above MMIOLIM to the same place

**`lapic.c:lapic_init()`**
```c
if(!lapic)
    lapic = mmio_map_region(lapicaddr, 4096);
```



---


### **Part A-II. booting an MP system**
* the bootstrap processor (BSP) (determined by the hardware) starts the other application processors (AP)
  * BSP control flow: `entry.S` --> `i386_init()` --> `boot_aps()` --> `lapic_startap()`
  * `boot_aps()` does the following to start each AP
    * copies the entry code `mpentry.S` embedded within the kernel to the physical memory starting at `MPENTRY_PADDR`
    * sets the stack for the entry code to execute on through the global variable `mpentry_kstack`
    * calls `lapic_startap()` to make the AP start up and execute the code at `MPENTRY_PADDR`
    * wait for the AP to finish booting up


* about `mpentry.S`
  * almost the same as `boot.S` for BSP
  * it needs to manually calculate symbol addresses because the code is compiled and linked with the kernel unlike `boot.S`
    * link address (address used during execution) can be different from load address (where in the phys mem sth is actually located)
    * link address (VM) = somewhere in the code section above `0xF0100000` (see `kernel.ld`)
    * load address (PM) = `MPENTRY_PADDR=0x7000`

* what are and should be unique to each CPU
  * kernel stack
    * because multiple CPU can be trapped into the kernel at the same time
    * even if we acquire a lock on entry to the kernel, there will be a race condition in pushing/popping the trapframes on the same stack
    * which can result in an incorrect execution state after an interrupt
  * TSS and TSS descriptor
    * because kernel stack is CPU-specific
  * current environment pointer
    * because we want each processor to be able to run different environments at the same time
  * registers
    * system registers (for page table/TSS/GDT/IDT/etc.) need to be loaded for each CPU


---



### **Exercise 2: modify physical page allocator to skip the page containing `MPENTRY_PADDR`**
answer skipped



---



### **Exercise 3 and 4: set up per-CPU kernel stack**

first comment out the original mono-core kernel stack vm mapping

**`pmap.c:mem_init()`**
```c
// commented out to support MP
// boot_map_region(kern_pgdir, KSTACKTOP-KSTKSIZE, KSTKSIZE, PADDR(bootstack), PTE_W);
```

now map kernel stacks for each possible CPU (the system supports up to `NCPU` CPUs)\
`cpus[cpunum()]`'s kernel stack is located at `PADDR(percpu_kstacks[cpunum()])` in physical memory

**`pmap.c`**
```c
static void
mem_init_mp(void)
{
    size_t i;
	for(i=0; i<NCPU; ++i)
	{
		uintptr_t kstacktop = KSTACKTOP-i*(KSTKSIZE + KSTKGAP);

		boot_map_region(kern_pgdir, 
			kstacktop-KSTKSIZE, 
			KSTKSIZE, 
			PADDR((void*)percpu_kstacks[i]),
			PTE_W);
	}
}
```

NOTE: if superpages (4MB) are used to map VA `[KERNBASE, 2^32)` to PA `[0, 2^32 - KERNBASE)`,\
we have to set the page size extension flag in the CR4 register for each CPU

**`pmap.c`**
```c
void
mem_init_percpu(void)
{
    //enable PSE
    uint32_t cr4 = rcr4();
    cr4 |= CR4_PSE;
    lcr4(cr4);
}
```

then, we need to set up a TSS and a TSS descriptor for each CPU so that the system knows which \
stack to switch to when entering the kernel from user space.

to do that, we first reserve `NCPU` entries (indexed from GD_TSS0) in `gdt` for per-cpu TSS descriptors

**`env.c`**
```c
struct Segdesc gdt[] =
{
	// 0x0 - unused (always faults -- for trapping NULL far pointers)
	SEG_NULL,

	// 0x8 - kernel code segment
	[GD_KT >> 3] = SEG(STA_X | STA_R, 0x0, 0xffffffff, 0),

	// 0x10 - kernel data segment
	[GD_KD >> 3] = SEG(STA_W, 0x0, 0xffffffff, 0),

	// 0x18 - user code segment
	[GD_UT >> 3] = SEG(STA_X | STA_R, 0x0, 0xffffffff, 3),

	// 0x20 - user data segment
	[GD_UD >> 3] = SEG(STA_W, 0x0, 0xffffffff, 3),

	// 0x28 to ((0x28 >> 3) + NCPU - 1) << 3
    // tss, initialized in trap_init_percpu()
	[(GD_TSS0 >> 3) + NCPU - 1] = SEG_NULL,
};
```

then, in `trap_init_percpu()`, we initialize the TSS in the `CpuInfo` structure \
and its corresponding TSS descriptor in `gdt`.

finally, we make the system registers point to right places.

**`trap.c`**
```c
// Initialize and load the per-CPU TSS and IDT
void
trap_init_percpu(void)
{
	struct CpuInfo *c = cpus + cpunum();
	int gdt_idx = (GD_TSS0 >> 3) + cpunum();

	c->cpu_ts.ts_esp0 = (uintptr_t) percpu_kstacks[cpunum()] + KSTKSIZE;
	c->cpu_ts.ts_ss0 = GD_KD;
	c->cpu_ts.ts_iomb = sizeof(struct Taskstate);

	// Initialize the TSS slot of the gdt.
	gdt[gdt_idx] = SEG16(STS_T32A, (uint32_t) (&c->cpu_ts),
					sizeof(struct Taskstate) - 1, 0);
	gdt[gdt_idx].sd_s = 0;

	// Load the TSS selector
	ltr((gdt_idx << 3) | (GD_TSS0 & 7));

	// Load the IDT
	lidt(&idt_pd);
}
```



---

### **Part A-III. kernel synchronization**
* because multiple CPUs can be trapped into the kernel at the same time, we need synchronization techniques to avoid race conditions.
  * simplest way: extreme coarse-grained locking
    * lock on entry to kernel, unlock on exit from kernel
    * entry point: `trap()`
    * exit point: `env_run()`
    * we also need to lock before the first scheduling round of each processor


---



### **Exercise 5: apply the big kernel lock**
answer skipped



---


### **Part A-IV. cooperative multitasking**
* the scheduler is responsible for determining which user environment to run when we return to user space
* in cooperative multitasking, a user process voluntarily gives up control to other user processes
  * the OS never initates a context switch

* `sched_yield()` is the entry point to the scheduler
  * exposed to user programs as a system call, allowing them to relinquish control

* in `env_run()`, why can the pointer e be dereferenced both before and after the addressing switch (loading the CR3 register)?
  * because every address space is set up to contain the kernel mapping

* Whenever the kernel switches from one environment to another, it must ensure the old environment's registers are saved so they can be restored properly later. Why? Where does this happen?
  * when trapped from user space, `trap()` stores the trapframe in the trapframe field of the current environment
  * `env_run()` takes a pointer to an `env` structure and returns to the state specified by the trapframe in it

---



### **Exercise 6: implement sched_yield() and make it a system call**
first we make the current env pointer to be cpu-specific, because each CPU can run different environments simultaneously

**`env.h`**
```c
//extern struct Env *curenv;		// Current environment
#define curenv (thiscpu->cpu_env) // The current env
```

then we implement a simple round-robin scheduler; it goes through all envs, starting from the env after the current one to the current one.

**`sched.c`**
```c
// Choose a user environment to run and run it.
void
sched_yield(void)
{
	struct Env *idle;
	struct Env *cur;

	//loop from curenv+1 to curenv over the whole envs array
	int start_idx = curenv ? curenv - envs + 1 : 0;
	int end_idx = curenv ? curenv - envs + NENV : NENV-1;
	int i;

	idle = NULL;
	for(i = start_idx; i <= end_idx; ++i)
	{
		cur = envs + (i % NENV);
		
		// either we are to be scheduled again (our status should be RUNNING) 
		// or we are to yield to another runnable env
		if(curenv == cur || cur->env_status == ENV_RUNNABLE)
		{
			idle = cur;
			break;
		}
	}

	if(!idle)
		// sched_halt never returns
		sched_halt();
	else
	{
		env_run(idle);
	}
}
```

finally, we hook it to the system call stub
**`kern/syscall.c`**
```c
static void
sys_yield(void)
{
	sched_yield();
}
```



---


### **Part A-V. env-related system calls**
* in this part, we give the user environments the abilities to allocate, map and unmap pages, set environment status, and create new environments through syscalls.
* a child environment may be created through sys_exofork()
    * it allocates a new environment as the child of the current one
    * it returns the env id of the child in parent, and 0 in child
    * the child env has almost the same register state (trapframe) as the current environment
    * except the value of %eax, which is the return value from sys_exofork()
    * the child env is not runnable because it does not have the user part of the address space set up



---



### **Exercise 7: implement sys_exofork, sys_env_set_status, sys_page_alloc, sys_page_map, sys_page_unmap**

below is an implementation for `sys_exofork()`; other functions are all fairly simple to implement and are omitted here.

**`kern/syscall.c`**
```c
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
```



---

### **Part B: Copy-On-Write Fork (COW Fork)**
### **Part B-I. user-level page fault handling**
* in this part, we give user programs the ability to handle page faults
  * a user program can set its pgfault handler through a syscall
  * if a handler is set, the kernel will invoke it upon any page fault that occurs in that user env, otherwise the user env will be destroyed
  * the handler function runs on an exception stack instead of the normal stack to prevent interference between normal code execution and fault handlers
  * the term "trap-time state" refers to the state of execution when an exception occurs (in this case, the page fault)
  * control flow when trapped from user space:
    * user space execution --> page fault --> switch to exception stack --> form arguments for the handler --> handler runs --> restore trap-time state --> switch to normal stack
  * control flow when recursive page fault happens (i.e. page fault happens in handler)
    * handler execution --> page fault --> continue on exception stack --> form arguments for the handler --> handler runs --> return to the previous handler

* the pagefault handler will have the following signature:
  * `void handler(struct UTrapframe *utf)`
  * `struct UTrapframe` contains the trap-time info deemed necessary for the handler



---



### **Exercise 9/10/11: set up the functionality described above**
* first, we need to make clear which parts of the control flow mentioned above happen in kernel space and which parts happen in user space

* what happens in the kernel: 
  * form arguments for the handler on the exception stack
  * set the return state for the user env to execute at the handler, on the exception stack

* what happens in the user space:
  * allocate and map the exception stack
  * restore trap-time state or return to the previous handler


---