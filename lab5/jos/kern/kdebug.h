#ifndef JOS_KERN_KDEBUG_H
#define JOS_KERN_KDEBUG_H

#include <inc/types.h>

// Debug information about a particular instruction pointer
struct Eipdebuginfo {
	const char *eip_file;		// Source code filename for EIP
	int eip_line;			// Source code linenumber for EIP

	const char *eip_fn_name;	// Name of function containing EIP
					//  - Note: not null terminated!
	int eip_fn_namelen;		// Length of function name
	uintptr_t eip_fn_addr;		// Address of start of function
	int eip_fn_narg;		// Number of function arguments
};

int debuginfo_eip(uintptr_t eip, struct Eipdebuginfo *info);

// Disassembler
typedef unsigned long bfd_vma;
typedef unsigned char bfd_byte;
typedef int (*fprintf_ftype) (void *, const char*, ...);

enum dis_insn_type
{
  dis_noninsn,			/* Not a valid instruction.  */
  dis_nonbranch,		/* Not a branch instruction.  */
  dis_branch,			/* Unconditional branch.  */
  dis_condbranch,		/* Conditional branch.  */
  dis_jsr,			/* Jump to subroutine.  */
  dis_condjsr,			/* Conditional jump to subroutine.  */
  dis_dref,			/* Data reference instruction.  */
  dis_dref2			/* Two data references in instruction.  */
};
struct disassemble_info
{
  fprintf_ftype fprintf_func;
  void *stream;
  void *application_data;

  /* The bfd_mach value.  */
  unsigned long mach;

  /* For use by the disassembler.
     The top 16 bits are reserved for public use (and are documented here).
     The bottom 16 bits are for the internal use of the disassembler.  */
  unsigned long flags;

  /* Use internally by the target specific disassembly code.  */
  void *private_data;

  /* Function used to get bytes to disassemble.  MEMADDR is the
     address of the stuff to be disassembled, MYADDR is the address to
     put the bytes in, and LENGTH is the number of bytes to read.
     INFO is a pointer to this struct.
     Returns an errno value or 0 for success.  */
  int (*read_memory_func)
    (bfd_vma memaddr, bfd_byte *myaddr, unsigned int length,
     struct disassemble_info *dinfo);

  /* Function which should be called if we get an error that we can't
     recover from.  STATUS is the errno value from read_memory_func and
     MEMADDR is the address that we were trying to read.  INFO is a
     pointer to this struct.  */
  void (*memory_error_func)
    (int status, bfd_vma memaddr, struct disassemble_info *dinfo);

  /* Function called to print ADDR.  */
  void (*print_address_func)
    (bfd_vma addr, struct disassemble_info *dinfo);

  /* This variable may be set by the instruction decoder.  It suggests
      the number of bytes objdump should display on a single line.  If
      the instruction decoder sets this, it should always set it to
      the same value in order to get reasonable looking output.  */
  int bytes_per_line;

  /* Results from instruction decoders.  Not all decoders yet support
     this information.  This info is set each time an instruction is
     decoded, and is only valid for the last such instruction.

     To determine whether this decoder supports this information, set
     insn_info_valid to 0, decode an instruction, then check it.  */

  char insn_info_valid;		/* Branch info has been set. */
  char branch_delay_insns;	/* How many sequential insn's will run before
				   a branch takes effect.  (0 = normal) */
  char data_size;		/* Size of data reference in insn, in bytes */
  enum dis_insn_type insn_type;	/* Type of instruction */
  bfd_vma target;		/* Target address of branch or dref, if known;
				   zero if unknown.  */
  bfd_vma target2;		/* Second target address for dref2 */

  /* Command line options specific to the target disassembler.  */
  const char *disassembler_options;
};
int print_insn_i386(bfd_vma vm, struct disassemble_info *info);

#endif
