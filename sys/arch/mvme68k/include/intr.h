/*	$NetBSD: intr.h,v 1.24 2024/01/19 03:09:05 thorpej Exp $	*/

/*-
 * Copyright (c) 2024 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MVME68K_INTR_H_
#define _MVME68K_INTR_H_

#ifdef _KERNEL

#include <m68k/psl.h>

#define	MACHINE_PSL_IPL_SOFTCLOCK	PSL_IPL1
#define	MACHINE_PSL_IPL_SOFTBIO		PSL_IPL1
#define	MACHINE_PSL_IPL_SOFTNET		PSL_IPL1
#define	MACHINE_PSL_IPL_SOFTSERIAL	PSL_IPL1
#define	MACHINE_PSL_IPL_VM		PSL_IPL3
#define	MACHINE_PSL_IPL_SCHED		PSL_IPL7

#define	MACHINE_INTREVCNT_NAMES						\
	{ "spur", "lev1", "lev2", "lev3", "lev4", "clock", "lev6", "nmi" }

#endif /* _KERNEL */

#include <m68k/intr.h>

#endif	/* _MVME68K_INTR_H */
