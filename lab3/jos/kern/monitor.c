// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>
#include <kern/pmap.h>
#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display stack backtrace", mon_backtrace },
	{ "pmap", "Display paging mapping information", mon_paginginfo },
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	return 0;
}

int
mon_paginginfo(int argc, char **argv, struct Trapframe *tf)
{
	uintptr_t start_va, end_va;
	uintptr_t *vals[2] = { &start_va, &end_va };

	int i;

	switch (argc)
	{
		case 1:
			start_va = 0;
			end_va = 0xffffffff;
			break;
		case 2:
			end_va = 0xffffffff;
		case 3:
			for (i = 1; i < argc; ++i)
			{
				char *num_str = strstr(argv[i], "0x");

				if (num_str)
				{
					if(num_str != argv[i])
						goto bad;
					num_str += 2;
				}
				else
				{
					num_str = strstr(argv[i], "0X");
					if (num_str)
					{
						if(num_str != argv[i])
							goto bad;
						num_str += 2;
					}
					else
					{
						num_str = argv[i];
					}
				}

				*vals[i-1] = (uintptr_t) strtol(num_str, NULL, 16);
			}

			if (start_va > end_va)
				goto bad;

			break;

		default:
			cprintf("mon_paginginfo: wrong number of arguments %d\n", argc);
			return 0;
		bad:
			cprintf("mon_paginginfo: invalid arguments\n");
			return 0;
	}

	int num_pages = (ROUNDDOWN(end_va, PGSIZE) - ROUNDDOWN(start_va, PGSIZE)) / PGSIZE + 1;
	uintptr_t va = ROUNDDOWN(start_va, PGSIZE);

	cprintf("[v start : v end] --> [p start : p end]      | p | w | usr | wt | cd | a | d | ps | g \n");

	for (i = 0; i < num_pages; ++i)
	{
		uint32_t *entry = 0;
		uintptr_t virt_page_addr;
		pde_t *pde = kern_pgdir + PDX(va);
		int sz;

		if(*pde & PTE_P)
		{
			if(*pde & PTE_PS)
			{
				sz = PTSIZE;
				entry = (uint32_t *)pde;
				virt_page_addr = PDX(va) << PDXSHIFT;
				
				i += NPTENTRIES;
				va = virt_page_addr + PTSIZE;
			}
			else
			{
				sz = PGSIZE;
				entry = (uint32_t *)pgdir_walk(kern_pgdir, (void *)va, 0);
				virt_page_addr = PGNUM(va) << PTXSHIFT;

				++i;
				va += PGSIZE;
			}
		}

		if (!entry)
		{
			cprintf("%08x not mapped\n", va);
		}
		else
		{
			cprintf("[%08x : %08x] --> [%08x : %08x] | %d | %d | %d | %d | %d | %d | %d | %d | %d \n", 
				virt_page_addr, virt_page_addr + (sz-1),
				PTE_ADDR(*entry), PTE_ADDR(*entry) + (sz-1),
				*entry & PTE_P ? 1 : 0,
				*entry & PTE_W ? 1 : 0,
				*entry & PTE_U ? 1 : 0,
				*entry & PTE_PWT ? 1 : 0,
				*entry & PTE_PCD ? 1 : 0,
				*entry & PTE_A ? 1 : 0,
				*entry & PTE_D ? 1 : 0,
				*entry & PTE_PS ? 1 : 0,
				*entry & PTE_G ? 1 : 0);
		}
	}

	return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
