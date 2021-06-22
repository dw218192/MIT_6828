#include <inc/mmu.h>
#include <inc/x86.h>
#include <inc/assert.h>

#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/env.h>
#include <kern/syscall.h>
#include <kern/cpu.h>
#include <kern/spinlock.h>
#include <kern/sched.h>

// lab4: commented out to support MP
static struct Taskstate ts;

/* For debugging, so print_trapframe can distinguish between printing
 * a saved trapframe and printing the current trapframe and print some
 * additional information in the latter case.
 */
static struct Trapframe *last_tf;

/* Interrupt descriptor table.  (Must be built at run time because
 * shifted function addresses can't be represented in relocation records.)
 */
struct Gatedesc idt[256] = { { 0 } };
struct Pseudodesc idt_pd = {
	sizeof(idt) - 1, (uint32_t) idt
};
extern uint32_t handler_addrs[];

static const char *trapname(int trapno)
{
	static const char * const excnames[] = {
		"Divide error",
		"Debug",
		"Non-Maskable Interrupt",
		"Breakpoint",
		"Overflow",
		"BOUND Range Exceeded",
		"Invalid Opcode",
		"Device Not Available",
		"Double Fault",
		"Coprocessor Segment Overrun",
		"Invalid TSS",
		"Segment Not Present",
		"Stack Fault",
		"General Protection",
		"Page Fault",
		"(unknown trap)",
		"x87 FPU Floating-Point Error",
		"Alignment Check",
		"Machine-Check",
		"SIMD Floating-Point Exception"
	};

	if (trapno < ARRAY_SIZE(excnames))
		return excnames[trapno];
	if (trapno == T_SYSCALL)
		return "System call";
	return "(unknown trap)";
}


void
trap_init(void)
{
	extern struct Segdesc gdt[];

	// LAB 3: Your code here.
	int i;
	for(i = 0; i < NPROCESSOR_TRAPS; ++i)
	{
		int dpl = 0;

		if(i == T_BRKPT)
			dpl = 3;
		
		SETGATE(idt[i], 0, GD_KT, handler_addrs[i], dpl);
	}

	extern void T_SYSCALL_HANDLER();
	SETGATE(idt[T_SYSCALL], 1, GD_KT, T_SYSCALL_HANDLER, 3);

	// Per-CPU setup 
	trap_init_percpu();
}

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

	// Load the TSS selector (like other segment selectors, the
	// bottom three bits are special; we leave them 0)
	ltr((gdt_idx << 3) | (GD_TSS0 & 7));

	// Load the IDT
	lidt(&idt_pd);
}

void
print_trapframe(struct Trapframe *tf)
{
	cprintf("TRAP frame at %p\n", tf);
	print_regs(&tf->tf_regs);
	cprintf("  es   0x----%04x\n", tf->tf_es);
	cprintf("  ds   0x----%04x\n", tf->tf_ds);
	cprintf("  trap 0x%08x %s\n", tf->tf_trapno, trapname(tf->tf_trapno));
	// If this trap was a page fault that just happened
	// (so %cr2 is meaningful), print the faulting linear address.
	if (tf == last_tf && tf->tf_trapno == T_PGFLT)
		cprintf("  cr2  0x%08x\n", rcr2());
	cprintf("  err  0x%08x", tf->tf_err);
	// For page faults, print decoded fault error code:
	// U/K=fault occurred in user/kernel mode
	// W/R=a write/read caused the fault
	// PR=a protection violation caused the fault (NP=page not present).
	if (tf->tf_trapno == T_PGFLT)
		cprintf(" [%s, %s, %s]\n",
			tf->tf_err & 4 ? "user" : "kernel",
			tf->tf_err & 2 ? "write" : "read",
			tf->tf_err & 1 ? "protection" : "not-present");
	else
		cprintf("\n");
	cprintf("  eip  0x%08x\n", tf->tf_eip);
	cprintf("  cs   0x----%04x\n", tf->tf_cs);
	cprintf("  flag 0x%08x\n", tf->tf_eflags);
	if ((tf->tf_cs & 3) != 0) {
		cprintf("  esp  0x%08x\n", tf->tf_esp);
		cprintf("  ss   0x----%04x\n", tf->tf_ss);
	}
}

void
print_regs(struct PushRegs *regs)
{
	cprintf("  edi  0x%08x\n", regs->reg_edi);
	cprintf("  esi  0x%08x\n", regs->reg_esi);
	cprintf("  ebp  0x%08x\n", regs->reg_ebp);
	cprintf("  oesp 0x%08x\n", regs->reg_oesp);
	cprintf("  ebx  0x%08x\n", regs->reg_ebx);
	cprintf("  edx  0x%08x\n", regs->reg_edx);
	cprintf("  ecx  0x%08x\n", regs->reg_ecx);
	cprintf("  eax  0x%08x\n", regs->reg_eax);
}

static void
trap_dispatch(struct Trapframe *tf)
{
	// Handle processor exceptions.
	// LAB 3: Your code here.
	
	switch(tf->tf_trapno)
	{
		case T_DEBUG:
		case T_BRKPT:
			monitor(tf);
			return;
		case T_PGFLT:
			page_fault_handler(tf);
			return;
		case T_SYSCALL:
			tf->tf_regs.reg_eax = syscall(
				tf->tf_regs.reg_eax,
				tf->tf_regs.reg_edx, 
				tf->tf_regs.reg_ecx, 
				tf->tf_regs.reg_ebx,
				tf->tf_regs.reg_edi, 
				tf->tf_regs.reg_esi);
			return;

		default:
			break;
	}

	// Unexpected trap: The user process or the kernel has a bug.
	print_trapframe(tf);

	if (tf->tf_cs == GD_KT)
		panic("unhandled trap in kernel");
	else {
		env_destroy(curenv);
		return;
	}
}

void
trap(struct Trapframe *tf)
{
	// The environment may have set DF and some versions
	// of GCC rely on DF being clear
	asm volatile("cld" ::: "cc");

	// Halt the CPU if some other CPU has called panic()
	extern char *panicstr;
	if (panicstr)
		asm volatile("hlt");
	
	// Re-acqurie the big kernel lock if we were halted in
	// sched_yield()
	if (xchg(&thiscpu->cpu_status, CPU_STARTED) == CPU_HALTED)
		lock_kernel();
	
	// Check that interrupts are disabled.  If this assertion
	// fails, DO NOT be tempted to fix it by inserting a "cli" in
	// the interrupt path.
	assert(!(read_eflags() & FL_IF));

	if ((tf->tf_cs & 3) == 3) {
		// Trapped from user mode.
		assert(curenv);

		lock_kernel();

		if (curenv->env_status == ENV_DYING) {
			env_free(curenv);
			curenv = NULL;
			sched_yield();
		}

		// Copy trap frame (which is currently on the stack)
		// into 'curenv->env_tf', so that running the environment
		// will restart at the trap point.
		curenv->env_tf = *tf;
		// The trapframe on the stack should be ignored from here on.
		tf = &curenv->env_tf;
	}

	// Record that tf is the last real trapframe so
	// print_trapframe can print some additional information.
	last_tf = tf;

	// Dispatch based on what type of trap occurred
	trap_dispatch(tf);

	// If we made it to this point, then no other environment was
	// scheduled, so we should return to the current environment
	// if doing so makes sense.
	if (curenv && curenv->env_status == ENV_RUNNING)
		env_run(curenv);
	else
		sched_yield();
}


void
page_fault_handler(struct Trapframe *tf)
{
	uint32_t fault_va;

	struct PageInfo *pinfo;
	uintptr_t kvm_uxstacktop; //top of user exception stack in kernel vm
	uintptr_t kvm_utf; //where to place utf

	// Read processor's CR2 register to find the faulting address
	fault_va = rcr2();

	// Handle kernel-mode page faults.
	// LAB 3: Your code here.
	if((tf->tf_cs & 3) == 0)
		panic("page_fault_handler: kernel page fault %08x", fault_va);

	// We've already handled kernel-mode exceptions, so if we get here,
	// the page fault happened in user mode.
	
	// if the user has set up a page fault handler
	if(curenv->env_pgfault_upcall)
	{
		// if the user has set up an exception stack
		if((pinfo = page_lookup(curenv->env_pgdir, (void*)(UXSTACKTOP-PGSIZE), 0)))
		{
			kvm_uxstacktop = (uintptr_t) page2kva(pinfo) + PGSIZE;
			// is this a recursive page fault?
			if(tf->tf_esp <= UXSTACKTOP-1 && tf->tf_esp >= UXSTACKTOP-PGSIZE)
			{
				kvm_utf = kvm_uxstacktop - (UXSTACKTOP - tf->tf_esp);
				// leave a scratch space (32 bits) for pfentry.S which will be pushing traptime eip at traptime esp
				kvm_utf -= 4;
			}
			else
			{
				kvm_utf = kvm_uxstacktop;
			}

			// allocate UTrapframe
			kvm_utf -= sizeof(struct UTrapframe);

			// set up UTrapframe
			struct UTrapframe* utf = (struct UTrapframe*)kvm_utf;
			utf->utf_esp = tf->tf_esp;
			utf->utf_eflags = tf->tf_eflags;
			utf->utf_eip = tf->tf_eip;
			utf->utf_err = tf->tf_err;
			utf->utf_fault_va = fault_va;
			utf->utf_regs = tf->tf_regs;

			// set return state for the user env
			tf->tf_esp = UXSTACKTOP - (kvm_uxstacktop - kvm_utf);
			tf->tf_eip = (uintptr_t) curenv->env_pgfault_upcall;

			return;
		}
	}

	// Destroy the environment that caused the fault.
	cprintf("[%08x] user fault va %08x ip %08x\n",
		curenv->env_id, fault_va, tf->tf_eip);
	print_trapframe(tf);
	env_destroy(curenv);
}

