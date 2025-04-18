/* $NetBSD: gicv3_its.h,v 1.10 2025/01/28 21:48:03 jmcneill Exp $ */

/*-
 * Copyright (c) 2018 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jared McNeill <jmcneill@invisible.ca>.
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

#ifndef _ARM_CORTEX_GICV3_ITS_H
#define _ARM_CORTEX_GICV3_ITS_H

#include <dev/pci/pcivar.h>

#include <arm/pci/pci_msi_machdep.h>
#include <arm/cortex/gic_reg.h>
#include <arm/cortex/gicv3.h>

struct gicv3_its_device {
	uint32_t		dev_id;
	u_int			dev_size;
	struct gicv3_dma	dev_itt;

	LIST_ENTRY(gicv3_its_device) dev_list;
};

struct gicv3_its_page_table {
	uint32_t		pt_index;
	struct gicv3_dma	pt_dma;
	LIST_ENTRY(gicv3_its_page_table) pt_list;
};

struct gicv3_its_table {
	void			*tab_l1;
	uint32_t		tab_page_size;
	uint64_t		tab_l1_entry_size;
	uint64_t		tab_l1_num_ids;
	uint64_t		tab_l2_entry_size;
	uint64_t		tab_l2_num_ids;
	bool			tab_indirect;
	bool			tab_shareable;

	LIST_HEAD(, gicv3_its_page_table) tab_pt;
};

struct gicv3_its {
	bus_space_tag_t		its_bst;
	bus_space_handle_t	its_bsh;
	bus_dma_tag_t		its_dmat;
	uint32_t		its_id;
	uint64_t		its_base;
	uint64_t		*its_rdbase;
	bool			*its_cpuonline;

	struct gicv3_softc	*its_gic;
	struct gicv3_lpi_callback its_cb;

	struct pic_softc	*its_pic;
	struct pci_attach_args	**its_pa;
	struct cpu_info		**its_targets;
	uint32_t		*its_devid;

	LIST_HEAD(, gicv3_its_device) its_devices;

	struct gicv3_dma	its_cmd;		/* Command queue */
	struct gicv3_dma	its_tab[8];		/* ITS tables */

	struct gicv3_its_table	its_tab_device;

	bool			its_cmd_flush;

	struct arm_pci_msi	its_msi;

	kmutex_t		*its_lock;
};

int	gicv3_its_init(struct gicv3_softc *, bus_space_handle_t, uint64_t, uint32_t);

#endif /* !_ARM_CORTEX_GICV3_ITS_H */
