// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/env.h>
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
	{ "s", "Step over", mon_step },
	{ "c", "Continue", mon_continue },
	{ "pmap", "Display paging mapping information", mon_paginginfo },
	{ "envls", "List all user environments", mon_envls },
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
	uintptr_t* ebp = (uintptr_t*) read_ebp();
	struct Eipdebuginfo info;
	int i;

	while (ebp)
	{
		uintptr_t eip = ebp[1];
		cprintf("  ebp %08x   eip %08x", ebp, eip);
		
		if(info.eip_fn_narg > 0)
		{
			cprintf(" args:");
			for (i=0; i<info.eip_fn_narg; ++i)
			{
				cprintf(" %08x", ebp[2+i]);
			}
		}
		cprintf("\n");

		if(debuginfo_eip(eip, &info) == 0)
		{
			cprintf("       %s:%d: %.*s+%d\n", 
				info.eip_file, info.eip_line, info.eip_fn_namelen, info.eip_fn_name, eip - info.eip_fn_addr);
		}
		else
		{
			cprintf("       unable to fetch function details\n");
		}

		ebp = (uintptr_t*) ebp[0];
	}

	return 0;
}

int
mon_step(int argc, char **argv, struct Trapframe *tf)
{
	if(tf->tf_trapno != T_BRKPT && tf->tf_trapno != T_DEBUG)
	{
		cprintf("must invoke single step at a breakpoint or debug trap\n");
		return 0;
	}

	//enable tf
	if(!(tf->tf_eflags & FL_TF))
	{
		tf->tf_eflags |= FL_TF;
	}

	//break out of monitor, give control back to trap_dispatch()
	return -1;
}

int
mon_continue(int argc, char **argv, struct Trapframe *tf)
{
	if(tf->tf_trapno != T_BRKPT && tf->tf_trapno != T_DEBUG)
	{
		cprintf("must invoke continue at a breakpoint or debug trap\n");
		return 0;
	}

	//disable tf
	if(tf->tf_eflags & FL_TF)
	{
		tf->tf_eflags ^= FL_TF;
	}

	//break out of monitor, give control back to trap_dispatch()
	return -1;
}

int
mon_paginginfo(int argc, char **argv, struct Trapframe *tf)
{
	uintptr_t start_va, end_va;
	uintptr_t *vals[2] = { &start_va, &end_va };

	int i; 
	int num_pages;
	uintptr_t va;
	
	uint32_t env_id;
	pde_t* pgdir = NULL;
	
	if(argc == 1)
	{
		cprintf("usage: pmap [envid, or k for kernel] [start va = 0] [end va = 0xffffffff] inclusive\n");
		return 0;
	}
	--argc;
	++argv;

	if((*argv)[0] == 'k' && (*argv)[1] == '\0')
	{
		pgdir = kern_pgdir;
	}
	else
	{
		env_id = strtol(*argv, NULL, 16);
		for(i=0; i<NENV; ++i)
		{
			if(envs[i].env_status != ENV_FREE && envs[i].env_status != ENV_DYING)
			{
				if(envs[i].env_id == env_id)
				{
					pgdir = envs[i].env_pgdir;
					break;
				}
			}
		}
	}
	--argc;
	++argv;

	if(!pgdir)
		goto bad;

	switch (argc)
	{
		case 0:
			start_va = 0;
			end_va = 0xffffffff;
			break;
		case 1:
			end_va = 0xffffffff;
		case 2:
			for (i = 0; i < argc; ++i)
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

				*vals[i] = (uintptr_t) strtol(num_str, NULL, 16);
			}

			if (start_va > end_va)
				goto bad;

			break;

		default:
			cprintf("mon_paginginfo: wrong number of arguments %d\n", argc);
			return 0;
	}


	num_pages = (ROUNDDOWN(end_va, PGSIZE) - ROUNDDOWN(start_va, PGSIZE)) / PGSIZE + 1;
	va = ROUNDDOWN(start_va, PGSIZE);

	cprintf("start=[%08x], end=[%08x], num_pages=[%d]\n", start_va, end_va, num_pages);
	static const char header_flags[] = "|  p|  w|usr| wt| cd|  a|  d| ps|  g|   avail|";
	static const char header_mapping[] = "[v start : v end] --> [p start : p end]";
	static const char fmt[] = "%50s   %60s\n";

	cprintf(fmt, header_mapping, header_flags);
	char buf1[50], buf2[60];
	
	for (i = 0; i < num_pages; )
	{
		uint32_t *entry = NULL;
		uintptr_t virt_page_addr;
		pde_t *pde = pgdir + PDX(va);
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
				entry = (uint32_t *)pgdir_walk(pgdir, (void *)va, 0);

				if(!(*entry & PTE_P))
					entry = NULL;
				
				virt_page_addr = PGNUM(va) << PTXSHIFT;

				++i;
				va += PGSIZE;
			}
		}
		else
		{
			virt_page_addr = PGNUM(va) << PTXSHIFT;
			++i;
			va += PGSIZE;
		}

		if (!entry)
		{
			cprintf("%08x not mapped\n", virt_page_addr);
		}
		else
		{
			snprintf(buf1, 50, "[%08x : %08x] --> [%08x : %08x]",
				virt_page_addr, virt_page_addr + (sz-1),
				PTE_ADDR(*entry), PTE_ADDR(*entry) + (sz-1));

			snprintf(buf2, 60, " | %d | %d | %d | %d | %d | %d | %d | %d | %d |%08x|", 
				*entry & PTE_P ? 1 : 0,
				*entry & PTE_W ? 1 : 0,
				*entry & PTE_U ? 1 : 0,
				*entry & PTE_PWT ? 1 : 0,
				*entry & PTE_PCD ? 1 : 0,
				*entry & PTE_A ? 1 : 0,
				*entry & PTE_D ? 1 : 0,
				*entry & PTE_PS ? 1 : 0,
				*entry & PTE_G ? 1 : 0,
				*entry & PTE_AVAIL);

			cprintf(fmt, buf1, buf2);
		}
	}

	return 0;

	bad:
		cprintf("mon_paginginfo: invalid arguments\n");
		return 0;
}

int
mon_envls(int argc, char **argv, struct Trapframe *tf)
{
	static const char * const env_status_names[] = 
	{
		[ENV_FREE] = "FREE",
		[ENV_DYING] = "DYING",
		[ENV_RUNNABLE] = "RUNNABLE",
		[ENV_RUNNING] = "RUNNING",
		[ENV_NOT_RUNNABLE] = "NOT_RUNNABLE",
	};
	static const char * const env_type_names[] = 
	{
		[ENV_TYPE_FS] = "FS",
		[ENV_TYPE_USER] = "USER",
	};
	int i;
	for(i=0; i<NENV; ++i)
	{
		if(envs[i].env_status != ENV_FREE && envs[i].env_status != ENV_DYING)
		{
			cprintf("env id=[%08x], parent id=[%08x], type=[%s] status=[%s], runs=[%08x]\n",
				envs[i].env_id,
				envs[i].env_parent_id,
				env_type_names[envs[i].env_type],
				env_status_names[envs[i].env_status],
				envs[i].env_runs
			);
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
