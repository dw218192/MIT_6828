/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>

.globl monitor

###################################################################
# exceptions/interrupts
###################################################################

/* start the function pointer table in the data section */
.data
	.globl handler_addrs
	.align 4
	handler_addrs:

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
.macro traphandler_pad
.data
	.long 0
.endm

.macro traphandler_noec_nodata name num
.text
	.globl \name
	.type \name, @function
	.align 2
	\name:
	pushl $0
	pushl $\num
	jmp _alltraps
.endm

.macro traphandler name num
.text
	.globl \name
	.type \name, @function
	.align 2
	\name:
	pushl $\num
	jmp _alltraps
.data
	.long \name
.endm

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
.macro traphandler_noec name num
.text
	traphandler_noec_nodata \name \num
.data
	.long \name
.endm

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */


/* software interrupts */
/* must use padding, to make the address fall into the right place in handler_addr array */
/* 0  */ traphandler_noec T_DIVIDE_HANDLER, T_DIVIDE
/* 1  */ traphandler_noec T_DEBUG_HANDLER, T_DEBUG
/* 2  */ traphandler_noec T_NMI_HANDLER, T_NMI
/* 3  */ traphandler_noec T_BRKPT_HANDLER, T_BRKPT
/* 4  */ traphandler_noec T_OFLOW_HANDLER, T_OFLOW
/* 5  */ traphandler_noec T_BOUND_HANDLER, T_BOUND
/* 6  */ traphandler_noec T_ILLOP_HANDLER, T_ILLOP
/* 7  */ traphandler_noec T_DEVICE_HANDLER, T_DEVICE
/* 8  */ traphandler T_DBLFLT_HANDLER, T_DBLFLT
/* 9  */ traphandler_pad
/* 10 */ traphandler T_TSS_HANDLER, T_TSS
/* 11 */ traphandler T_SEGNP_HANDLER, T_SEGNP
/* 12 */ traphandler T_STACK_HANDLER, T_STACK
/* 13 */ traphandler T_GPFLT_HANDLER, T_GPFLT
/* 14 */ traphandler T_PGFLT_HANDLER, T_PGFLT
/* 15 */ traphandler_pad
/* 16 */ traphandler_noec T_FPERR_HANDLER, T_FPERR
/* 17 */ traphandler T_ALIGN_HANDLER, T_ALIGN
/* 18 */ traphandler_noec T_MCHK_HANDLER, T_MCHK
/* 19 */ traphandler_noec T_SIMDERR_HANDLER, T_SIMDERR

/* not setting the handler_addr after this point */
/* handler addresses below are hardcoded by the C code in trap.c */

/* syscall handler */
traphandler_noec_nodata T_SYSCALL_HANDLER, T_SYSCALL

/* hardware interrupts */
traphandler_noec_nodata T_IRQ_TIMER_HANDLER, (IRQ_OFFSET+IRQ_TIMER)
traphandler_noec_nodata T_IRQ_KBD_HANDLER, (IRQ_OFFSET+IRQ_KBD)
traphandler_noec_nodata T_IRQ_SERIAL_HANDLER, (IRQ_OFFSET+IRQ_SERIAL)
traphandler_noec_nodata T_IRQ_SPURIOUS_HANDLER, (IRQ_OFFSET+IRQ_SPURIOUS)
traphandler_noec_nodata T_IRQ_IDE_HANDLER, (IRQ_OFFSET+IRQ_IDE)
traphandler_noec_nodata T_IRQ_ERROR_HANDLER, (IRQ_OFFSET+IRQ_ERROR)

/*
 * Lab 3: Your code here for _alltraps
 */

.text
	_alltraps:
	/* trapno should already been pushed */
	pushw $0 		/* tf_padding2 */
	pushw %ds       /* tf_ds */
	pushw $0        /* tf_padding1 */
	pushw %es       /* tf_es */
	pushal 			/* struct pushregs */

	pushw $GD_KD
	popw  %ds
	pushw $GD_KD
	popw  %es

	movl  0x8(%esp), %ebp /* fake frame pointer for backtrace */
	pushl %esp /* argument to trap */
	call trap /* does not return */
	
	/* if trap returns, panic */
spin: /* while (1) monitor(NULL) */
	sub    $0xc, %esp
	push   $0x0
	call   monitor
	add    $0x10, %esp
	jmp    spin