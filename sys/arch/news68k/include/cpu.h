/*	$NetBSD: cpu.h,v 1.55 2024/01/20 00:15:32 thorpej Exp $	*/

/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1982, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * from: Utah $Hdr: cpu.h 1.16 91/03/25$
 *
 *	@(#)cpu.h	8.4 (Berkeley) 1/5/94
 */

#ifndef _NEWS68K_CPU_H_
#define _NEWS68K_CPU_H_

#if defined(_KERNEL_OPT)
#include "opt_lockdebug.h"
#include "opt_newsconf.h"
#endif

/*
 * Get common m68k CPU definitions.
 */
#include <m68k/cpu.h>

#if defined(_KERNEL)
/*
 * Exported definitions unique to news68k cpu support.
 */

/*
 * XXX news1700 L2 cache would be corrupted with DC_BE and IC_BE...
 * XXX Should these be defined in machine/cpu.h?
 */
#undef CACHE_ON
#undef CACHE_CLR
#undef IC_CLEAR
#undef DC_CLEAR
#define CACHE_ON	(DC_WA|DC_CLR|DC_ENABLE|IC_CLR|IC_ENABLE)
#define CACHE_CLR	CACHE_ON
#define IC_CLEAR	(DC_WA|DC_ENABLE|IC_CLR|IC_ENABLE)
#define DC_CLEAR	(DC_WA|DC_CLR|DC_ENABLE|IC_ENABLE)

#define DCIC_CLR	(DC_CLR|IC_CLR)
#define CACHE_BE	(DC_BE|IC_BE)

#define	cpu_set_hw_ast(l)						\
	do {								\
		extern volatile u_char *ctrl_ast;			\
		__USE(l);						\
		*ctrl_ast = 0xff;					\
	} while (/*CONSTCOND*/0)

#if defined(news1700)
#define CACHE_HAVE_PAC
#endif

extern int systype;
#define NEWS1700	0
#define NEWS1200	1

extern int cpuspeed;
extern char *intiobase, *intiolimit, *extiobase;
extern u_int intiobase_phys, intiotop_phys;
extern u_int extiobase_phys, extiotop_phys;

extern void *romcallvec;

struct frame;

void doboot(int)
	__attribute__((__noreturn__));
void nmihand(struct frame *);
void ecacheon(void);
void ecacheoff(void);

/* machdep.c functions */
int badaddr(void *, int);
int badbaddr(void *);

#endif

/* physical memory sections */
#define ROMBASE		0xe0000000

#define INTIOBASE1700	0xe0c00000
#define INTIOTOP1700	0xe1d00000 /* XXX */
#define EXTIOBASE1700	0xf0f00000
#define EXTIOTOP1700	0xf1000000 /* XXX */
#define CTRL_POWER1700	0xe1380000
#define CTRL_LED1700	0xe0dc0000

#define INTIOBASE1200	0xe1000000
#define INTIOTOP1200	0xe1d00000 /* XXX */
#define EXTIOBASE1200	0xe4000000
#define EXTIOTOP1200	0xe4020000 /* XXX */
#define CTRL_POWER1200	0xe1000000
#define CTRL_LED1200	0xe1500001

#define MAXADDR		0xfffff000

/*
 * Internal IO space:
 *
 * Internal IO space is mapped in the kernel from ``intiobase'' to
 * ``intiolimit'' (defined in locore.s).  Since it is always mapped,
 * conversion between physical and kernel virtual addresses is easy.
 */
#define ISIIOVA(va) \
	((char *)(va) >= intiobase && (char *)(va) < intiolimit)
#define IIOV(pa)	(((u_int)(pa) - intiobase_phys) + (u_int)intiobase)
#define ISIIOPA(pa) \
	((u_int)(pa) >= intiobase_phys && (u_int)(pa) < intiotop_phys)
#define IIOP(va)	(((u_int)(va) - (u_int)intiobase) + intiobase_phys)
#define IIOPOFF(pa)	((u_int)(pa) - intiobase_phys)

/* XXX EIO space mapping should be modified like hp300 XXX */
#define	EIOSIZE		(extiotop_phys - extiobase_phys)
#define ISEIOVA(va) \
	((char *)(va) >= extiobase && (char *)(va) < (char *)EIOSIZE)
#define EIOV(pa)	(((u_int)(pa) - extiobase_phys) + (u_int)extiobase)

#if defined(CACHE_HAVE_PAC) || defined(CACHE_HAVE_VAC)
#define M68K_CACHEOPS_MACHDEP
#endif

#ifdef CACHE_HAVE_PAC
#define M68K_CACHEOPS_MACHDEP_PCIA
#endif

#ifdef CACHE_HAVE_VAC
#define M68K_CACHEOPS_MACHDEP_DCIA
#define M68K_CACHEOPS_MACHDEP_DCIS
#define M68K_CACHEOPS_MACHDEP_DCIU
#endif

#endif /* !_NEWS68K_CPU_H_ */
