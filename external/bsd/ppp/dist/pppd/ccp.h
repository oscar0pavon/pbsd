/*	$NetBSD: ccp.h,v 1.6 2025/01/08 19:59:38 christos Exp $	*/

/*
 * ccp.h - Definitions for PPP Compression Control Protocol.
 *
 * Copyright (c) 1994-2024 Paul Mackerras. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THE AUTHORS OF THIS SOFTWARE DISCLAIM ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef PPP_CCP_H
#define PPP_CCP_H

#include "pppdconf.h"

#ifdef  __cplusplus
extern "C" {
#endif

typedef struct ccp_options {
    bool bsd_compress;		/* do BSD Compress? */
    bool deflate;		/* do Deflate? */
    bool predictor_1;		/* do Predictor-1? */
    bool predictor_2;		/* do Predictor-2? */
    bool deflate_correct;	/* use correct code for deflate? */
    bool deflate_draft;		/* use draft RFC code for deflate? */
    unsigned char mppe;		/* MPPE bitfield */
    unsigned short bsd_bits;		/* # bits/code for BSD Compress */
    unsigned short deflate_size;	/* lg(window size) for Deflate */
    short method;		/* code for chosen compression method */
} ccp_options;

extern fsm ccp_fsm[];
extern ccp_options ccp_wantoptions[];
extern ccp_options ccp_gotoptions[];
extern ccp_options ccp_allowoptions[];
extern ccp_options ccp_hisoptions[];

extern struct protent ccp_protent;

#ifdef __cplusplus
}
#endif

#endif // PPP_CCP_H
