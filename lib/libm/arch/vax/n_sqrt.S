/*	$NetBSD: n_sqrt.S,v 1.12 2024/05/07 15:15:10 riastradh Exp $	*/
/*
 * Copyright (c) 1985, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)sqrt.s	8.1 (Berkeley) 6/4/93
 */

#include <machine/asm.h>

#ifdef WEAK_ALIAS
WEAK_ALIAS(_sqrtl, sqrt)
WEAK_ALIAS(sqrtl, sqrt)
#endif

/*
 * double sqrt(arg)   revised August 15,1982
 * double arg;
 * if(arg<0.0) { _errno = EDOM; return(<a reserved operand>); }
 * if arg is a reserved operand it is returned as it is
 * W. Kahan's magic square root
 * coded by Heidi Stettner and revised by Emile LeBlanc 8/18/82
 *
 * entry points:_d_sqrt		address of double arg is on the stack
 *		_sqrt		double arg is on the stack
 */
	.set	EDOM,33

ENTRY(d_sqrt, 0x003c)		# save %r5,%r4,%r3,%r2
	movq	*4(%ap),%r0
	jbr  	dsqrt2
END(d_sqrt)

ENTRY(sqrt, 0x003c)		# save %r5,%r4,%r3,%r2
	movq    4(%ap),%r0

dsqrt2:	bicw3	$0x807f,%r0,%r2	# check exponent of input
	jeql	noexp		# biased exponent is zero -> 0.0 or reserved
	bsbb	__libm_dsqrt_r5_lcl
noexp:	ret
END(sqrt)

/* **************************** internal procedure */

	.hidden __libm_dsqrt_r5
ALTENTRY(__libm_dsqrt_r5)
	halt
	halt
__libm_dsqrt_r5_lcl:
				/* ENTRY POINT FOR cdabs and cdsqrt	*/
				/* returns double square root scaled by	*/
				/* 2^%r6	*/

	movd	%r0,%r4
	jleq	nonpos		# argument is not positive
	movzwl	%r4,%r2
	ashl	$-1,%r2,%r0
	addw2	$0x203c,%r0	# %r0 has magic initial approximation
/*
 * Do two steps of Heron's rule
 * ((arg/guess) + guess) / 2 = better guess
 */
	divf3	%r0,%r4,%r2
	addf2	%r2,%r0
	subw2	$0x80,%r0	# divide by two

	divf3	%r0,%r4,%r2
	addf2	%r2,%r0
	subw2	$0x80,%r0	# divide by two

/* Scale argument and approximation to prevent over/underflow */

	bicw3	$0x807f,%r4,%r1
	subw2	$0x4080,%r1		# %r1 contains scaling factor
	subw2	%r1,%r4
	movl	%r0,%r2
	subw2	%r1,%r2

/* Cubic step
 *
 * b = a + 2*a*(n-a*a)/(n+3*a*a) where b is better approximation,
 * a is approximation, and n is the original argument.
 * (let s be scale factor in the following comments)
 */
	clrl	%r1
	clrl	%r3
	muld2	%r0,%r2			# %r2:%r3 = a*a/s
	subd2	%r2,%r4			# %r4:%r5 = n/s - a*a/s
	addw2	$0x100,%r2		# %r2:%r3 = 4*a*a/s
	addd2	%r4,%r2			# %r2:%r3 = n/s + 3*a*a/s
	muld2	%r0,%r4			# %r4:%r5 = a*n/s - a*a*a/s
	divd2	%r2,%r4			# %r4:%r5 = a*(n-a*a)/(n+3*a*a)
	addw2	$0x80,%r4		# %r4:%r5 = 2*a*(n-a*a)/(n+3*a*a)
	addd2	%r4,%r0			# %r0:%r1 = a + 2*a*(n-a*a)/(n+3*a*a)
	rsb				# DONE!
nonpos:
	jneq	negarg
	ret				# argument and root are zero
negarg:
	pushl	$EDOM
	calls	$1,_C_LABEL(infnan)	# generate the reserved op fault
	ret

ENTRY(sqrtf, 0)
	cvtfd	4(%ap),-(%sp)
	calls	$2,_C_LABEL(sqrt)
	cvtdf	%r0,%r0
	ret
END(sqrtf)
