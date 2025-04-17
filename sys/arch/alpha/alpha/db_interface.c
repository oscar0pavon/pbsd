/* $NetBSD: db_interface.c,v 1.43 2024/02/05 22:08:04 andvar Exp $ */

/*
 * Mach Operating System
 * Copyright (c) 1992,1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS ``AS IS''
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 *
 *	db_interface.c,v 2.4 1991/02/05 17:11:13 mrt (CMU)
 */

/*
 * Parts of this file are derived from Mach 3:
 *
 *	File: alpha_instruction.c
 *	Author: Alessandro Forin, Carnegie Mellon University
 *	Date:	6/92
 */

/*
 * Interface to DDB.
 *
 * Modified for NetBSD/alpha by:
 *
 *	Christopher G. Demetriou, Carnegie Mellon University
 *
 *	Jason R. Thorpe, Numerical Aerospace Simulation Facility,
 *	NASA Ames Research Center
 */

#ifdef _KERNEL_OPT
#include "opt_ddb.h"
#include "opt_multiprocessor.h"
#endif

#include <sys/cdefs.h>			/* RCS ID & Copyright macro defns */

__KERNEL_RCSID(0, "$NetBSD: db_interface.c,v 1.43 2024/02/05 22:08:04 andvar Exp $");

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <sys/systm.h>

#include <dev/cons.h>

#include <machine/alpha.h>
#include <machine/db_machdep.h>
#include <machine/pal.h>
#include <machine/prom.h>

#include <machine/alpha_instruction.h>

#include <ddb/db_user.h>
#include <ddb/db_active.h>
#include <ddb/db_sym.h>
#include <ddb/db_command.h>
#include <ddb/db_extern.h>
#include <ddb/db_access.h>
#include <ddb/db_output.h>
#include <ddb/db_variables.h>
#include <ddb/db_interface.h>

#if 0
extern char *trap_type[];
extern int trap_types;
#endif

int	db_active = 0;

db_regs_t *ddb_regp;

#if defined(MULTIPROCESSOR)
void	db_mach_cpu(db_expr_t, bool, db_expr_t, const char *);
#endif

const struct db_command db_machine_command_table[] = {
#if defined(MULTIPROCESSOR)
	{ DDB_ADD_CMD("cpu",	db_mach_cpu,	0,
	  "switch to another cpu", "cpu-no", NULL) },
#endif
	{ DDB_END_CMD },
};

static int db_alpha_regop(const struct db_variable *, db_expr_t *, int);

#define	dbreg(xx)	((long *)(xx))

#define	DBREG(n, r)						\
	{	.name = __STRING(n),				\
		.valuep = ((long *)(r)),			\
		.fcn = db_alpha_regop,				\
		.modif = NULL, }

const struct db_variable db_regs[] = {
	DBREG(v0,	FRAME_V0),
	DBREG(t0,	FRAME_T0),
	DBREG(t1,	FRAME_T1),
	DBREG(t2,	FRAME_T2),
	DBREG(t3,	FRAME_T3),
	DBREG(t4,	FRAME_T4),
	DBREG(t5,	FRAME_T5),
	DBREG(t6,	FRAME_T6),
	DBREG(t7,	FRAME_T7),
	DBREG(s0,	FRAME_S0),
	DBREG(s1,	FRAME_S1),
	DBREG(s2,	FRAME_S2),
	DBREG(s3,	FRAME_S3),
	DBREG(s4,	FRAME_S4),
	DBREG(s5,	FRAME_S5),
	DBREG(s6,	FRAME_S6),
	DBREG(a0,	FRAME_A0),
	DBREG(a1,	FRAME_A1),
	DBREG(a2,	FRAME_A2),
	DBREG(a3,	FRAME_A3),
	DBREG(a4,	FRAME_A4),
	DBREG(a5,	FRAME_A5),
	DBREG(t8,	FRAME_T8),
	DBREG(t9,	FRAME_T9),
	DBREG(t10,	FRAME_T10),
	DBREG(t11,	FRAME_T11),
	DBREG(ra,	FRAME_RA),
	DBREG(t12,	FRAME_T12),
	DBREG(at,	FRAME_AT),
	DBREG(gp,	FRAME_GP),
	DBREG(sp,	FRAME_SP),
	DBREG(pc,	FRAME_PC),
	DBREG(ps,	FRAME_PS),
	DBREG(ai,	FRAME_T11),
	DBREG(pv,	FRAME_T12),
};
const struct db_variable * const db_eregs = db_regs + sizeof(db_regs)/sizeof(db_regs[0]);

#undef DBREG

static int
db_alpha_regop(const struct db_variable *vp, db_expr_t *val, int opcode)
{
	unsigned long *tfaddr;
	unsigned long zeroval = 0;
	struct trapframe *f = NULL;

#ifdef _KERNEL			/* XXX ?? */
	if (vp->modif != NULL && *vp->modif == 'u') {
		if (curlwp != NULL)
			f = curlwp->l_md.md_tf;
	} else
#endif /* _KERNEL */
		f = DDB_REGS;
	tfaddr = f == NULL ? &zeroval : &f->tf_regs[(u_long)vp->valuep];
	switch (opcode) {
	case DB_VAR_GET:
		*val = *tfaddr;
		break;

	case DB_VAR_SET:
		*tfaddr = *val;
		break;

	default:
#ifdef _KERNEL
		panic("db_alpha_regop: unknown op %d", opcode);
#endif
		break;
	}

	return (0);
}

#ifdef _KERNEL
/*
 * ddb_trap - field a kernel trap
 */
int
ddb_trap(unsigned long a0, unsigned long a1, unsigned long a2, unsigned long entry, db_regs_t *regs)
{
	struct cpu_info *ci = curcpu();
	unsigned long psl;

	if (entry != ALPHA_KENTRY_IF ||
	    (a0 != ALPHA_IF_CODE_BPT && a0 != ALPHA_IF_CODE_BUGCHK)) {
		if (db_recover != 0) {
			/* This will longjmp back into db_command_loop() */
			db_error("Caught exception in ddb.\n");
			/* NOTREACHED */
		}

		/*
		 * Tell caller "We did NOT handle the trap."
		 * Caller should panic, or whatever.
		 */
		return (0);
	}

	/*
	 * alpha_debug() switches us to the debugger stack.
	 */

	/* Our register state is simply the trapframe. */
	ddb_regp = ci->ci_db_regs = regs;

	/*
	 * Use SWPIPL directly; we want to avoid processing
	 * software interrupts when we go back.  Soft ints
	 * will be caught later, so not to worry.
	 */
	psl = alpha_pal_swpipl(ALPHA_PSL_IPL_HIGH);

	db_active++;
	cnpollc(true);		/* Set polling mode, unblank video */

	db_trap(entry, a0);	/* Where the work happens */

	cnpollc(false);		/* Resume interrupt mode */
	db_active--;

	alpha_pal_swpipl(psl);

	ddb_regp = ci->ci_db_regs = NULL;

	/*
	 * Tell caller "We HAVE handled the trap."
	 */
	return (1);
}

/*
 * Read bytes from kernel address space for debugger.
 */
void
db_read_bytes(vaddr_t addr, register size_t size, register char *data)
{
	register char	*src;

	src = (char *)addr;
	while (size-- > 0)
		*data++ = *src++;
}

/*
 * Write bytes to kernel address space for debugger.
 */
void
db_write_bytes(vaddr_t addr, register size_t size, register const char *data)
{
	register char	*dst;

	dst = (char *)addr;
	while (size-- > 0)
		*dst++ = *data++;
	alpha_pal_imb();
}

void
cpu_Debugger(void)
{

	__asm volatile("call_pal 0x81");		/* bugchk */
}
#endif /* _KERNEL */

/*
 * Alpha-specific ddb commands:
 *
 *	cpu		tell DDB to use register state from the
 *			CPU specified (MULTIPROCESSOR)
 */

#if defined(MULTIPROCESSOR)
void
db_mach_cpu(db_expr_t addr, bool have_addr, db_expr_t count, const char * modif)
{
	struct cpu_info *ci;

	if (!have_addr) {
		cpu_debug_dump();
		return;
	}

	if (addr < 0 || addr >= ALPHA_MAXPROCS) {
		db_printf("CPU %ld out of range\n", addr);
		return;
	}

	ci = cpu_info[addr];
	if (ci == NULL) {
		db_printf("CPU %ld is not configured\n", addr);
		return;
	}

	if (ci != curcpu()) {
		if ((ci->ci_flags & CPUF_PAUSED) == 0) {
			db_printf("CPU %ld not paused\n", addr);
			return;
		}
	}

	if (ci->ci_db_regs == NULL) {
		db_printf("CPU %ld has no register state\n", addr);
		return;
	}

	db_printf("Using CPU %ld\n", addr);
	ddb_regp = ci->ci_db_regs;
}
#endif /* MULTIPROCESSOR */

/*
 * Map Alpha register numbers to trapframe/db_regs_t offsets.
 */
static int reg_to_frame[32] = {
	FRAME_V0,
	FRAME_T0,
	FRAME_T1,
	FRAME_T2,
	FRAME_T3,
	FRAME_T4,
	FRAME_T5,
	FRAME_T6,
	FRAME_T7,

	FRAME_S0,
	FRAME_S1,
	FRAME_S2,
	FRAME_S3,
	FRAME_S4,
	FRAME_S5,
	FRAME_S6,

	FRAME_A0,
	FRAME_A1,
	FRAME_A2,
	FRAME_A3,
	FRAME_A4,
	FRAME_A5,

	FRAME_T8,
	FRAME_T9,
	FRAME_T10,
	FRAME_T11,
	FRAME_RA,
	FRAME_T12,
	FRAME_AT,
	FRAME_GP,
	FRAME_SP,
	-1,		/* zero */
};

u_long
db_register_value(db_regs_t *regs, int regno)
{

	if (regno > 31 || regno < 0) {
		db_printf(" **** STRANGE REGISTER NUMBER %d **** ", regno);
		return (0);
	}

	if (regno == 31)
		return (0);

	return (regs->tf_regs[reg_to_frame[regno]]);
}

#ifdef _KERNEL
/*
 * Support functions for software single-step.
 */

bool
db_inst_call(int ins)
{
	alpha_instruction insn;

	insn.bits = ins;
	return ((insn.branch_format.opcode == op_bsr) ||
	    ((insn.jump_format.opcode == op_j) &&
	     (insn.jump_format.action & 1)));
}

bool
db_inst_return(int ins)
{
	alpha_instruction insn;

	insn.bits = ins;
	return ((insn.jump_format.opcode == op_j) &&
	    (insn.jump_format.action == op_ret));
}

bool
db_inst_trap_return(int ins)
{
	alpha_instruction insn;

	insn.bits = ins;
	return ((insn.pal_format.opcode == op_pal) &&
	    (insn.pal_format.function == PAL_OSF1_rti));
}

bool
db_inst_branch(int ins)
{
	alpha_instruction insn;

	insn.bits = ins;
	switch (insn.branch_format.opcode) {
	case op_j:
	case op_br:
	case op_fbeq:
	case op_fblt:
	case op_fble:
	case op_fbne:
	case op_fbge:
	case op_fbgt:
	case op_blbc:
	case op_beq:
	case op_blt:
	case op_ble:
	case op_blbs:
	case op_bne:
	case op_bge:
	case op_bgt:
		return (true);
	}

	return (false);
}

bool
db_inst_unconditional_flow_transfer(int ins)
{
	alpha_instruction insn;

	insn.bits = ins;
	switch (insn.branch_format.opcode) {
	case op_j:
	case op_br:
		return (true);

	case op_pal:
		switch (insn.pal_format.function) {
		case PAL_OSF1_retsys:
		case PAL_OSF1_rti:
		case PAL_OSF1_callsys:
			return (true);
		}
	}

	return (false);
}

#if 0
bool
db_inst_spill(int ins, int regn)
{
	alpha_instruction insn;

	insn.bits = ins;
	return ((insn.mem_format.opcode == op_stq) &&
	    (insn.mem_format.rd == regn));
}
#endif

bool
db_inst_load(int ins)
{
	alpha_instruction insn;

	insn.bits = ins;
	
	/* Loads. */
	if (insn.mem_format.opcode == op_ldbu ||
	    insn.mem_format.opcode == op_ldq_u ||
	    insn.mem_format.opcode == op_ldwu)
		return (true);
	if ((insn.mem_format.opcode >= op_ldf) &&
	    (insn.mem_format.opcode <= op_ldt))
		return (true);
	if ((insn.mem_format.opcode >= op_ldl) &&
	    (insn.mem_format.opcode <= op_ldq_l))
		return (true);

	/* Prefetches. */
	if (insn.mem_format.opcode == op_special) {
		/* Note: MB is treated as a store. */
		if ((insn.mem_format.displacement == (short)op_fetch) ||
		    (insn.mem_format.displacement == (short)op_fetch_m))
			return (true);
	}

	return (false);
}

bool
db_inst_store(int ins)
{
	alpha_instruction insn;

	insn.bits = ins;

	/* Stores. */
	if (insn.mem_format.opcode == op_stw ||
	    insn.mem_format.opcode == op_stb ||
	    insn.mem_format.opcode == op_stq_u)
		return (true);
	if ((insn.mem_format.opcode >= op_stf) &&
	    (insn.mem_format.opcode <= op_stt))
		return (true);
	if ((insn.mem_format.opcode >= op_stl) &&
	    (insn.mem_format.opcode <= op_stq_c))
		return (true);

	/* Barriers. */
	if (insn.mem_format.opcode == op_special) {
		if (insn.mem_format.displacement == op_mb)
			return (true);
	}

	return (false);
}

db_addr_t
db_branch_taken(int ins, db_addr_t pc, db_regs_t *regs)
{
	long signed_immediate;
	alpha_instruction insn;
	db_addr_t newpc;

	insn.bits = ins;
	switch (insn.branch_format.opcode) {
	/*
	 * Jump format: target PC is (contents of instruction's "RB") & ~3.
	 */
	case op_j:
		newpc = db_register_value(regs, insn.jump_format.rb) & ~3;
		break;

	/*
	 * Branch format: target PC is
	 *	(new PC) + (4 * sign-ext(displacement)).
	 */
	case op_br:
	case op_fbeq:
	case op_fblt:
	case op_fble:
	case op_bsr:
	case op_fbne:
	case op_fbge:
	case op_fbgt:
	case op_blbc:
	case op_beq:
	case op_blt:
	case op_ble:
	case op_blbs:
	case op_bne:
	case op_bge:
	case op_bgt:
		signed_immediate = insn.branch_format.displacement;
		newpc = (pc + 4) + (signed_immediate << 2);
		break;

	default:
		printf("DDB: db_inst_branch_taken on non-branch!\n");
		newpc = pc;	/* XXX */
	}

	return (newpc);
}
#endif /* _KERNEL */

unsigned long
db_alpha_read_saved_reg(unsigned long *regp)
{
	unsigned long reg;

	db_read_bytes((db_addr_t)regp, sizeof(reg), (char *)&reg);
	return reg;
}

unsigned long
db_alpha_tf_reg(struct trapframe *tf, unsigned int regno)
{
	return db_alpha_read_saved_reg(&tf->tf_regs[regno]);
}

/*
 * Alpha special symbol handling.
 */
db_alpha_nlist db_alpha_nl[] = {
	DB_ALPHA_SYM(SYM_XentArith, XentArith),
	DB_ALPHA_SYM(SYM_XentIF, XentIF),
	DB_ALPHA_SYM(SYM_XentInt, XentInt),
	DB_ALPHA_SYM(SYM_XentMM, XentMM),
	DB_ALPHA_SYM(SYM_XentSys, XentSys),
	DB_ALPHA_SYM(SYM_XentUna, XentUna),
	DB_ALPHA_SYM(SYM_XentRestart, XentRestart),
	DB_ALPHA_SYM(SYM_exception_return, exception_return),
	DB_ALPHA_SYM(SYM_alpha_kthread_backstop, alpha_kthread_backstop),
#ifndef _KERNEL
	DB_ALPHA_SYM(SYM_dumppcb, dumppcb),
#endif /* _KERNEL */
	DB_ALPHA_SYM_EOL
};

static int
db_alpha_nlist_lookup(db_addr_t addr)
{
	int i;

	for (i = 0; i < SYM___eol; i++) {
		if (db_alpha_nl[i].n_value == addr) {
			return i;
		}
	}
	return -1;
}

bool
db_alpha_sym_is_trap(db_addr_t addr)
{
	int i = db_alpha_nlist_lookup(addr);
	return i >= SYM_XentArith && i <= SYM_exception_return;
}

bool
db_alpha_sym_is_backstop(db_addr_t addr)
{
	return db_alpha_nlist_lookup(addr) == SYM_alpha_kthread_backstop;
}

bool
db_alpha_sym_is_syscall(db_addr_t addr)
{
	return db_alpha_nlist_lookup(addr) == SYM_XentSys;
}

const char *
db_alpha_trapsym_description(db_addr_t addr)
{
	static const char * const trap_descriptions[] = {
	[SYM_XentArith]		=	"arithmetic trap",
	[SYM_XentIF]		=	"instruction fault",
	[SYM_XentInt]		=	"interrupt",
	[SYM_XentMM]		=	"memory management fault",
	[SYM_XentSys]		=	"syscall",
	[SYM_XentUna]		=	"unaligned access fault",
	[SYM_XentRestart]	=	"console restart",
	[SYM_exception_return]	=	"(exception return)",
	};

	int i = db_alpha_nlist_lookup(addr);
	if (i >= SYM_XentArith && i <= SYM_exception_return) {
		return trap_descriptions[i];
	}
	return "??? trap ???";
}
