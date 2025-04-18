/*	$NetBSD: dk.c,v 1.173 2025/04/13 14:01:00 jakllsch Exp $	*/

/*-
 * Copyright (c) 2004, 2005, 2006, 2007 The NetBSD Foundation, Inc.
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

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: dk.c,v 1.173 2025/04/13 14:01:00 jakllsch Exp $");

#ifdef _KERNEL_OPT
#include "opt_dkwedge.h"
#endif

#include <sys/param.h>
#include <sys/types.h>

#include <sys/buf.h>
#include <sys/bufq.h>
#include <sys/callout.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/disk.h>
#include <sys/disklabel.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/kauth.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/vnode.h>

#include <miscfs/specfs/specdev.h>

MALLOC_DEFINE(M_DKWEDGE, "dkwedge", "Disk wedge structures");

typedef enum {
	DKW_STATE_LARVAL	= 0,
	DKW_STATE_RUNNING	= 1,
	DKW_STATE_DYING		= 2,
	DKW_STATE_DEAD		= 666
} dkwedge_state_t;

/*
 * Lock order:
 *
 *	sc->sc_dk.dk_openlock
 *	=> sc->sc_parent->dk_rawlock
 *	=> sc->sc_parent->dk_openlock
 *	=> dkwedges_lock
 *	=> sc->sc_sizelock
 *
 * Locking notes:
 *
 *	W	dkwedges_lock
 *	D	device reference
 *	O	sc->sc_dk.dk_openlock
 *	P	sc->sc_parent->dk_openlock
 *	R	sc->sc_parent->dk_rawlock
 *	S	sc->sc_sizelock
 *	I	sc->sc_iolock
 *	$	stable after initialization
 *	1	used only by a single thread
 *
 * x&y means both x and y must be held to write (with a write lock if
 * one is rwlock), and either x or y must be held to read.
 */

struct dkwedge_softc {
	device_t	sc_dev;	/* P&W: pointer to our pseudo-device */
		/* sc_dev is also stable while device is referenced */
	struct cfdata	sc_cfdata;	/* 1: our cfdata structure */
	uint8_t		sc_wname[128];	/* $: wedge name (Unicode, UTF-8) */

	dkwedge_state_t sc_state;	/* state this wedge is in */
		/* stable while device is referenced */
		/* used only in assertions when stable, and in dump in ddb */

	struct disk	*sc_parent;	/* $: parent disk */
		/* P: sc_parent->dk_openmask */
		/* P: sc_parent->dk_nwedges */
		/* P: sc_parent->dk_wedges */
		/* R: sc_parent->dk_rawopens */
		/* R: sc_parent->dk_rawvp (also stable while wedge is open) */
	daddr_t		sc_offset;	/* $: LBA offset of wedge in parent */
	krwlock_t	sc_sizelock;
	uint64_t	sc_size;	/* S: size of wedge in blocks */
	char		sc_ptype[32];	/* $: partition type */
	dev_t		sc_pdev;	/* $: cached parent's dev_t */
					/* P: link on parent's wedge list */
	LIST_ENTRY(dkwedge_softc) sc_plink;

	struct disk	sc_dk;		/* our own disk structure */
		/* O&R: sc_dk.dk_bopenmask */
		/* O&R: sc_dk.dk_copenmask */
		/* O&R: sc_dk.dk_openmask */
	struct bufq_state *sc_bufq;	/* $: buffer queue */
	struct callout	sc_restart_ch;	/* I: callout to restart I/O */

	kmutex_t	sc_iolock;
	bool		sc_iostop;	/* I: don't schedule restart */
	int		sc_mode;	/* O&R: parent open mode */
};

static int	dkwedge_match(device_t, cfdata_t, void *);
static void	dkwedge_attach(device_t, device_t, void *);
static int	dkwedge_detach(device_t, int);

static void	dk_set_geometry(struct dkwedge_softc *, struct disk *);

static void	dkstart(struct dkwedge_softc *);
static void	dkiodone(struct buf *);
static void	dkrestart(void *);
static void	dkminphys(struct buf *);

static int	dkfirstopen(struct dkwedge_softc *, int);
static void	dklastclose(struct dkwedge_softc *);
static int	dkwedge_detach(device_t, int);
static void	dkwedge_delall1(struct disk *, bool);
static int	dkwedge_del1(struct dkwedge_info *, int);
static int	dk_open_parent(dev_t, int, struct vnode **);
static int	dk_close_parent(struct vnode *, int);

static dev_type_open(dkopen);
static dev_type_close(dkclose);
static dev_type_cancel(dkcancel);
static dev_type_read(dkread);
static dev_type_write(dkwrite);
static dev_type_ioctl(dkioctl);
static dev_type_strategy(dkstrategy);
static dev_type_dump(dkdump);
static dev_type_size(dksize);
static dev_type_discard(dkdiscard);

CFDRIVER_DECL(dk, DV_DISK, NULL);
CFATTACH_DECL3_NEW(dk, 0,
    dkwedge_match, dkwedge_attach, dkwedge_detach, NULL, NULL, NULL,
    DVF_DETACH_SHUTDOWN);

const struct bdevsw dk_bdevsw = {
	.d_open = dkopen,
	.d_close = dkclose,
	.d_cancel = dkcancel,
	.d_strategy = dkstrategy,
	.d_ioctl = dkioctl,
	.d_dump = dkdump,
	.d_psize = dksize,
	.d_discard = dkdiscard,
	.d_cfdriver = &dk_cd,
	.d_devtounit = dev_minor_unit,
	.d_flag = D_DISK | D_MPSAFE
};

const struct cdevsw dk_cdevsw = {
	.d_open = dkopen,
	.d_close = dkclose,
	.d_cancel = dkcancel,
	.d_read = dkread,
	.d_write = dkwrite,
	.d_ioctl = dkioctl,
	.d_stop = nostop,
	.d_tty = notty,
	.d_poll = nopoll,
	.d_mmap = nommap,
	.d_kqfilter = nokqfilter,
	.d_discard = dkdiscard,
	.d_cfdriver = &dk_cd,
	.d_devtounit = dev_minor_unit,
	.d_flag = D_DISK | D_MPSAFE
};

static struct dkwedge_softc **dkwedges;
static u_int ndkwedges;
static krwlock_t dkwedges_lock;

static LIST_HEAD(, dkwedge_discovery_method) dkwedge_discovery_methods;
static krwlock_t dkwedge_discovery_methods_lock;

/*
 * dkwedge_match:
 *
 *	Autoconfiguration match function for pseudo-device glue.
 */
static int
dkwedge_match(device_t parent, cfdata_t match, void *aux)
{

	/* Pseudo-device; always present. */
	return 1;
}

/*
 * dkwedge_attach:
 *
 *	Autoconfiguration attach function for pseudo-device glue.
 */
static void
dkwedge_attach(device_t parent, device_t self, void *aux)
{
	struct dkwedge_softc *sc = aux;
	struct disk *pdk = sc->sc_parent;
	int unit = device_unit(self);

	KASSERTMSG(unit >= 0, "unit=%d", unit);

	if (!pmf_device_register(self, NULL, NULL))
		aprint_error_dev(self, "couldn't establish power handler\n");

	mutex_enter(&pdk->dk_openlock);
	rw_enter(&dkwedges_lock, RW_WRITER);
	KASSERTMSG(unit < ndkwedges, "unit=%d ndkwedges=%u", unit, ndkwedges);
	KASSERTMSG(sc == dkwedges[unit], "sc=%p dkwedges[%d]=%p",
	    sc, unit, dkwedges[unit]);
	KASSERTMSG(sc->sc_dev == NULL, "sc=%p sc->sc_dev=%p", sc, sc->sc_dev);
	sc->sc_dev = self;
	rw_exit(&dkwedges_lock);
	mutex_exit(&pdk->dk_openlock);

	disk_init(&sc->sc_dk, device_xname(sc->sc_dev), NULL);
	mutex_enter(&pdk->dk_openlock);
	dk_set_geometry(sc, pdk);
	mutex_exit(&pdk->dk_openlock);
	disk_attach(&sc->sc_dk);

	/* Disk wedge is ready for use! */
	device_set_private(self, sc);
	sc->sc_state = DKW_STATE_RUNNING;
}

/*
 * dkwedge_compute_pdev:
 *
 *	Compute the parent disk's dev_t.
 */
static int
dkwedge_compute_pdev(const char *pname, dev_t *pdevp, enum vtype type)
{
	const char *name, *cp;
	devmajor_t pmaj;
	int punit;
	char devname[16];

	name = pname;
	switch (type) {
	case VBLK:
		pmaj = devsw_name2blk(name, devname, sizeof(devname));
		break;
	case VCHR:
		pmaj = devsw_name2chr(name, devname, sizeof(devname));
		break;
	default:
		pmaj = NODEVMAJOR;
		break;
	}
	if (pmaj == NODEVMAJOR)
		return ENXIO;

	name += strlen(devname);
	for (cp = name, punit = 0; *cp >= '0' && *cp <= '9'; cp++)
		punit = (punit * 10) + (*cp - '0');
	if (cp == name) {
		/* Invalid parent disk name. */
		return ENXIO;
	}

	*pdevp = MAKEDISKDEV(pmaj, punit, RAW_PART);

	return 0;
}

/*
 * dkwedge_array_expand:
 *
 *	Expand the dkwedges array.
 *
 *	Releases and reacquires dkwedges_lock as a writer.
 */
static int
dkwedge_array_expand(void)
{

	const unsigned incr = 16;
	unsigned newcnt, oldcnt;
	struct dkwedge_softc **newarray = NULL, **oldarray = NULL;

	KASSERT(rw_write_held(&dkwedges_lock));

	oldcnt = ndkwedges;
	oldarray = dkwedges;

	if (oldcnt >= INT_MAX - incr)
		return ENFILE;	/* XXX */
	newcnt = oldcnt + incr;

	rw_exit(&dkwedges_lock);
	newarray = malloc(newcnt * sizeof(*newarray), M_DKWEDGE,
	    M_WAITOK|M_ZERO);
	rw_enter(&dkwedges_lock, RW_WRITER);

	if (ndkwedges != oldcnt || dkwedges != oldarray) {
		oldarray = NULL; /* already recycled */
		goto out;
	}

	if (oldarray != NULL)
		memcpy(newarray, dkwedges, ndkwedges * sizeof(*newarray));
	dkwedges = newarray;
	newarray = NULL;	/* transferred to dkwedges */
	ndkwedges = newcnt;

out:	rw_exit(&dkwedges_lock);
	if (oldarray != NULL)
		free(oldarray, M_DKWEDGE);
	if (newarray != NULL)
		free(newarray, M_DKWEDGE);
	rw_enter(&dkwedges_lock, RW_WRITER);
	return 0;
}

static void
dkwedge_size_init(struct dkwedge_softc *sc, uint64_t size)
{

	rw_init(&sc->sc_sizelock);
	sc->sc_size = size;
}

static void
dkwedge_size_fini(struct dkwedge_softc *sc)
{

	rw_destroy(&sc->sc_sizelock);
}

static uint64_t
dkwedge_size(struct dkwedge_softc *sc)
{
	uint64_t size;

	rw_enter(&sc->sc_sizelock, RW_READER);
	size = sc->sc_size;
	rw_exit(&sc->sc_sizelock);

	return size;
}

static void
dkwedge_size_increase(struct dkwedge_softc *sc, uint64_t size)
{

	KASSERT(mutex_owned(&sc->sc_parent->dk_openlock));

	rw_enter(&sc->sc_sizelock, RW_WRITER);
	KASSERTMSG(size >= sc->sc_size,
	    "decreasing dkwedge size from %"PRIu64" to %"PRIu64,
	    sc->sc_size, size);
	sc->sc_size = size;
	rw_exit(&sc->sc_sizelock);
}

static void
dk_set_geometry(struct dkwedge_softc *sc, struct disk *pdk)
{
	struct disk *dk = &sc->sc_dk;
	struct disk_geom *dg = &dk->dk_geom;
	uint32_t r, lspps;

	KASSERT(mutex_owned(&pdk->dk_openlock));

	memset(dg, 0, sizeof(*dg));

	dg->dg_secperunit = dkwedge_size(sc);
	dg->dg_secsize = DEV_BSIZE << pdk->dk_blkshift;

	/* fake numbers, 1 cylinder is 1 MB with default sector size */
	dg->dg_nsectors = 32;
	dg->dg_ntracks = 64;
	dg->dg_ncylinders =
	    dg->dg_secperunit / (dg->dg_nsectors * dg->dg_ntracks);

	dg->dg_physsecsize = pdk->dk_geom.dg_physsecsize;
	dg->dg_alignedsec = pdk->dk_geom.dg_alignedsec;
	lspps = MAX(1u, dg->dg_physsecsize / dg->dg_secsize);
	r = sc->sc_offset % lspps;
	if (r > dg->dg_alignedsec)
		dg->dg_alignedsec += lspps;
	dg->dg_alignedsec -= r;
	dg->dg_alignedsec %= lspps;

	disk_set_info(sc->sc_dev, dk, NULL);
}

/*
 * dkwedge_add:		[exported function]
 *
 *	Add a disk wedge based on the provided information.
 *
 *	The incoming dkw_devname[] is ignored, instead being
 *	filled in and returned to the caller.
 */
int
dkwedge_add(struct dkwedge_info *dkw)
{
	struct dkwedge_softc *sc, *lsc;
	struct disk *pdk;
	u_int unit;
	int error;
	dev_t pdev;
	device_t dev __diagused;

	dkw->dkw_parent[sizeof(dkw->dkw_parent) - 1] = '\0';
	pdk = disk_find(dkw->dkw_parent);
	if (pdk == NULL)
		return ENXIO;

	error = dkwedge_compute_pdev(pdk->dk_name, &pdev, VBLK);
	if (error)
		return error;

	if (dkw->dkw_offset < 0)
		return EINVAL;

	/*
	 * Check for an existing wedge at the same disk offset. Allow
	 * updating a wedge if the only change is the size, and the new
	 * size is larger than the old.
	 */
	sc = NULL;
	mutex_enter(&pdk->dk_openlock);
	LIST_FOREACH(lsc, &pdk->dk_wedges, sc_plink) {
		if (lsc->sc_offset != dkw->dkw_offset)
			continue;
		if (strcmp(lsc->sc_wname, dkw->dkw_wname) != 0)
			break;
		if (strcmp(lsc->sc_ptype, dkw->dkw_ptype) != 0)
			break;
		if (dkwedge_size(lsc) > dkw->dkw_size)
			break;
		if (lsc->sc_dev == NULL)
			break;

		sc = lsc;
		device_acquire(sc->sc_dev);
		dkwedge_size_increase(sc, dkw->dkw_size);
		dk_set_geometry(sc, pdk);

		break;
	}
	mutex_exit(&pdk->dk_openlock);

	if (sc != NULL)
		goto announce;

	sc = malloc(sizeof(*sc), M_DKWEDGE, M_WAITOK|M_ZERO);
	sc->sc_state = DKW_STATE_LARVAL;
	sc->sc_parent = pdk;
	sc->sc_pdev = pdev;
	sc->sc_offset = dkw->dkw_offset;
	dkwedge_size_init(sc, dkw->dkw_size);

	memcpy(sc->sc_wname, dkw->dkw_wname, sizeof(sc->sc_wname));
	sc->sc_wname[sizeof(sc->sc_wname) - 1] = '\0';

	memcpy(sc->sc_ptype, dkw->dkw_ptype, sizeof(sc->sc_ptype));
	sc->sc_ptype[sizeof(sc->sc_ptype) - 1] = '\0';

	bufq_alloc(&sc->sc_bufq, "fcfs", 0);

	callout_init(&sc->sc_restart_ch, 0);
	callout_setfunc(&sc->sc_restart_ch, dkrestart, sc);

	mutex_init(&sc->sc_iolock, MUTEX_DEFAULT, IPL_BIO);

	/*
	 * Wedge will be added; increment the wedge count for the parent.
	 * Only allow this to happen if RAW_PART is the only thing open.
	 */
	mutex_enter(&pdk->dk_openlock);
	if (pdk->dk_openmask & ~(1 << RAW_PART))
		error = EBUSY;
	else {
		/* Check for wedge overlap. */
		LIST_FOREACH(lsc, &pdk->dk_wedges, sc_plink) {
			/* XXX arithmetic overflow */
			uint64_t size = dkwedge_size(sc);
			uint64_t lsize = dkwedge_size(lsc);
			daddr_t lastblk = sc->sc_offset + size - 1;
			daddr_t llastblk = lsc->sc_offset + lsize - 1;

			if (sc->sc_offset >= lsc->sc_offset &&
			    sc->sc_offset <= llastblk) {
				/* Overlaps the tail of the existing wedge. */
				break;
			}
			if (lastblk >= lsc->sc_offset &&
			    lastblk <= llastblk) {
				/* Overlaps the head of the existing wedge. */
			    	break;
			}
		}
		if (lsc != NULL) {
			if (sc->sc_offset == lsc->sc_offset &&
			    dkwedge_size(sc) == dkwedge_size(lsc) &&
			    strcmp(sc->sc_wname, lsc->sc_wname) == 0)
				error = EEXIST;
			else
				error = EINVAL;
		} else {
			pdk->dk_nwedges++;
			LIST_INSERT_HEAD(&pdk->dk_wedges, sc, sc_plink);
		}
	}
	mutex_exit(&pdk->dk_openlock);
	if (error) {
		mutex_destroy(&sc->sc_iolock);
		bufq_free(sc->sc_bufq);
		dkwedge_size_fini(sc);
		free(sc, M_DKWEDGE);
		return error;
	}

	/* Fill in our cfdata for the pseudo-device glue. */
	sc->sc_cfdata.cf_name = dk_cd.cd_name;
	sc->sc_cfdata.cf_atname = dk_ca.ca_name;
	/* sc->sc_cfdata.cf_unit set below */
	sc->sc_cfdata.cf_fstate = FSTATE_NOTFOUND; /* use chosen cf_unit */

	/* Insert the larval wedge into the array. */
	rw_enter(&dkwedges_lock, RW_WRITER);
	for (error = 0;;) {
		struct dkwedge_softc **scpp;

		/*
		 * Check for a duplicate wname while searching for
		 * a slot.
		 */
		for (scpp = NULL, unit = 0; unit < ndkwedges; unit++) {
			if (dkwedges[unit] == NULL) {
				if (scpp == NULL) {
					scpp = &dkwedges[unit];
					sc->sc_cfdata.cf_unit = unit;
				}
			} else {
				/* XXX Unicode. */
				if (strcmp(dkwedges[unit]->sc_wname,
					sc->sc_wname) == 0) {
					error = EEXIST;
					break;
				}
			}
		}
		if (error)
			break;
		KASSERT(unit == ndkwedges);
		if (scpp == NULL) {
			error = dkwedge_array_expand();
			if (error)
				break;
		} else {
			KASSERT(scpp == &dkwedges[sc->sc_cfdata.cf_unit]);
			*scpp = sc;
			break;
		}
	}
	rw_exit(&dkwedges_lock);
	if (error) {
		mutex_enter(&pdk->dk_openlock);
		pdk->dk_nwedges--;
		LIST_REMOVE(sc, sc_plink);
		mutex_exit(&pdk->dk_openlock);

		mutex_destroy(&sc->sc_iolock);
		bufq_free(sc->sc_bufq);
		dkwedge_size_fini(sc);
		free(sc, M_DKWEDGE);
		return error;
	}

	/*
	 * Now that we know the unit #, attach a pseudo-device for
	 * this wedge instance.  This will provide us with the
	 * device_t necessary for glue to other parts of the system.
	 *
	 * This should never fail, unless we're almost totally out of
	 * memory.
	 */
	if ((dev = config_attach_pseudo_acquire(&sc->sc_cfdata, sc)) == NULL) {
		aprint_error("%s%u: unable to attach pseudo-device\n",
		    sc->sc_cfdata.cf_name, sc->sc_cfdata.cf_unit);

		rw_enter(&dkwedges_lock, RW_WRITER);
		KASSERT(dkwedges[sc->sc_cfdata.cf_unit] == sc);
		dkwedges[sc->sc_cfdata.cf_unit] = NULL;
		rw_exit(&dkwedges_lock);

		mutex_enter(&pdk->dk_openlock);
		pdk->dk_nwedges--;
		LIST_REMOVE(sc, sc_plink);
		mutex_exit(&pdk->dk_openlock);

		mutex_destroy(&sc->sc_iolock);
		bufq_free(sc->sc_bufq);
		dkwedge_size_fini(sc);
		free(sc, M_DKWEDGE);
		return ENOMEM;
	}

	KASSERT(dev == sc->sc_dev);

announce:
	/* Announce our arrival. */
	aprint_normal(
	    "%s at %s: \"%s\", %"PRIu64" blocks at %"PRId64", type: %s\n",
	    device_xname(sc->sc_dev), pdk->dk_name,
	    sc->sc_wname,	/* XXX Unicode */
	    dkwedge_size(sc), sc->sc_offset,
	    sc->sc_ptype[0] == '\0' ? "<unknown>" : sc->sc_ptype);

	/* Return the devname to the caller. */
	strlcpy(dkw->dkw_devname, device_xname(sc->sc_dev),
	    sizeof(dkw->dkw_devname));

	device_release(sc->sc_dev);
	return 0;
}

/*
 * dkwedge_find_acquire:
 *
 *	Lookup a disk wedge based on the provided information.
 *	NOTE: We look up the wedge based on the wedge devname,
 *	not wname.
 *
 *	Return NULL if the wedge is not found, otherwise return
 *	the wedge's softc.  Assign the wedge's unit number to unitp
 *	if unitp is not NULL.  The wedge's sc_dev is referenced and
 *	must be released by device_release or equivalent.
 */
static struct dkwedge_softc *
dkwedge_find_acquire(struct dkwedge_info *dkw, u_int *unitp)
{
	struct dkwedge_softc *sc = NULL;
	u_int unit;

	/* Find our softc. */
	dkw->dkw_devname[sizeof(dkw->dkw_devname) - 1] = '\0';
	rw_enter(&dkwedges_lock, RW_READER);
	for (unit = 0; unit < ndkwedges; unit++) {
		if ((sc = dkwedges[unit]) != NULL &&
		    sc->sc_dev != NULL &&
		    strcmp(device_xname(sc->sc_dev), dkw->dkw_devname) == 0 &&
		    strcmp(sc->sc_parent->dk_name, dkw->dkw_parent) == 0) {
			device_acquire(sc->sc_dev);
			break;
		}
	}
	rw_exit(&dkwedges_lock);
	if (sc == NULL)
		return NULL;

	if (unitp != NULL)
		*unitp = unit;

	return sc;
}

/*
 * dkwedge_del:		[exported function]
 *
 *	Delete a disk wedge based on the provided information.
 *	NOTE: We look up the wedge based on the wedge devname,
 *	not wname.
 */
int
dkwedge_del(struct dkwedge_info *dkw)
{

	return dkwedge_del1(dkw, 0);
}

int
dkwedge_del1(struct dkwedge_info *dkw, int flags)
{
	struct dkwedge_softc *sc = NULL;

	/* Find our softc. */
	if ((sc = dkwedge_find_acquire(dkw, NULL)) == NULL)
		return ESRCH;

	return config_detach_release(sc->sc_dev, flags);
}

/*
 * dkwedge_detach:
 *
 *	Autoconfiguration detach function for pseudo-device glue.
 */
static int
dkwedge_detach(device_t self, int flags)
{
	struct dkwedge_softc *const sc = device_private(self);
	const u_int unit = device_unit(self);
	int bmaj, cmaj, error;

	error = disk_begindetach(&sc->sc_dk, /*lastclose*/NULL, self, flags);
	if (error)
		return error;

	/* Mark the wedge as dying. */
	sc->sc_state = DKW_STATE_DYING;

	pmf_device_deregister(self);

	/* Kill any pending restart. */
	mutex_enter(&sc->sc_iolock);
	sc->sc_iostop = true;
	mutex_exit(&sc->sc_iolock);
	callout_halt(&sc->sc_restart_ch, NULL);

	/* Locate the wedge major numbers. */
	bmaj = bdevsw_lookup_major(&dk_bdevsw);
	cmaj = cdevsw_lookup_major(&dk_cdevsw);

	/* Nuke the vnodes for any open instances. */
	vdevgone(bmaj, unit, unit, VBLK);
	vdevgone(cmaj, unit, unit, VCHR);

	/*
	 * At this point, all block device opens have been closed,
	 * synchronously flushing any buffered writes; and all
	 * character device I/O operations have completed
	 * synchronously, and character device opens have been closed.
	 *
	 * So there can be no more opens or queued buffers by now.
	 */
	KASSERT(sc->sc_dk.dk_openmask == 0);
	KASSERT(bufq_peek(sc->sc_bufq) == NULL);
	bufq_drain(sc->sc_bufq);

	/* Announce our departure. */
	aprint_normal("%s at %s (%s) deleted\n", device_xname(sc->sc_dev),
	    sc->sc_parent->dk_name,
	    sc->sc_wname);	/* XXX Unicode */

	mutex_enter(&sc->sc_parent->dk_openlock);
	sc->sc_parent->dk_nwedges--;
	LIST_REMOVE(sc, sc_plink);
	mutex_exit(&sc->sc_parent->dk_openlock);

	/* Delete our buffer queue. */
	bufq_free(sc->sc_bufq);

	/* Detach from the disk list. */
	disk_detach(&sc->sc_dk);
	disk_destroy(&sc->sc_dk);

	/* Poof. */
	rw_enter(&dkwedges_lock, RW_WRITER);
	KASSERT(dkwedges[unit] == sc);
	dkwedges[unit] = NULL;
	sc->sc_state = DKW_STATE_DEAD;
	rw_exit(&dkwedges_lock);

	mutex_destroy(&sc->sc_iolock);
	dkwedge_size_fini(sc);

	free(sc, M_DKWEDGE);

	return 0;
}

/*
 * dkwedge_delall:	[exported function]
 *
 *	Forcibly delete all of the wedges on the specified disk.  Used
 *	when a disk is being detached.
 */
void
dkwedge_delall(struct disk *pdk)
{

	dkwedge_delall1(pdk, /*idleonly*/false);
}

/*
 * dkwedge_delidle:	[exported function]
 *
 *	Delete all of the wedges on the specified disk if idle.  Used
 *	by ioctl(DIOCRMWEDGES).
 */
void
dkwedge_delidle(struct disk *pdk)
{

	dkwedge_delall1(pdk, /*idleonly*/true);
}

static void
dkwedge_delall1(struct disk *pdk, bool idleonly)
{
	struct dkwedge_softc *sc;
	int flags;

	flags = DETACH_QUIET;
	if (!idleonly)
		flags |= DETACH_FORCE;

	for (;;) {
		mutex_enter(&pdk->dk_rawlock); /* for sc->sc_dk.dk_openmask */
		mutex_enter(&pdk->dk_openlock);
		LIST_FOREACH(sc, &pdk->dk_wedges, sc_plink) {
			/*
			 * Wedge is not yet created.  This is a race --
			 * it may as well have been added just after we
			 * deleted all the wedges, so pretend it's not
			 * here yet.
			 */
			if (sc->sc_dev == NULL)
				continue;
			if (!idleonly || sc->sc_dk.dk_openmask == 0) {
				device_acquire(sc->sc_dev);
				break;
			}
		}
		if (sc == NULL) {
			KASSERT(idleonly || pdk->dk_nwedges == 0);
			mutex_exit(&pdk->dk_openlock);
			mutex_exit(&pdk->dk_rawlock);
			return;
		}
		mutex_exit(&pdk->dk_openlock);
		mutex_exit(&pdk->dk_rawlock);
		(void)config_detach_release(sc->sc_dev, flags);
	}
}

/*
 * dkwedge_list:	[exported function]
 *
 *	List all of the wedges on a particular disk.
 */
int
dkwedge_list(struct disk *pdk, struct dkwedge_list *dkwl, struct lwp *l)
{
	struct uio uio;
	struct iovec iov;
	struct dkwedge_softc *sc;
	struct dkwedge_info dkw;
	int error = 0;

	iov.iov_base = dkwl->dkwl_buf;
	iov.iov_len = dkwl->dkwl_bufsize;

	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = 0;
	uio.uio_resid = dkwl->dkwl_bufsize;
	uio.uio_rw = UIO_READ;
	KASSERT(l == curlwp);
	uio.uio_vmspace = l->l_proc->p_vmspace;

	dkwl->dkwl_ncopied = 0;

	mutex_enter(&pdk->dk_openlock);
	LIST_FOREACH(sc, &pdk->dk_wedges, sc_plink) {
		if (uio.uio_resid < sizeof(dkw))
			break;

		if (sc->sc_dev == NULL)
			continue;

		strlcpy(dkw.dkw_devname, device_xname(sc->sc_dev),
		    sizeof(dkw.dkw_devname));
		memcpy(dkw.dkw_wname, sc->sc_wname, sizeof(dkw.dkw_wname));
		dkw.dkw_wname[sizeof(dkw.dkw_wname) - 1] = '\0';
		strlcpy(dkw.dkw_parent, sc->sc_parent->dk_name,
		    sizeof(dkw.dkw_parent));
		dkw.dkw_offset = sc->sc_offset;
		dkw.dkw_size = dkwedge_size(sc);
		strlcpy(dkw.dkw_ptype, sc->sc_ptype, sizeof(dkw.dkw_ptype));

		/*
		 * Acquire a device reference so this wedge doesn't go
		 * away before our next iteration in LIST_FOREACH, and
		 * then release the lock for uiomove.
		 */
		device_acquire(sc->sc_dev);
		mutex_exit(&pdk->dk_openlock);
		error = uiomove(&dkw, sizeof(dkw), &uio);
		mutex_enter(&pdk->dk_openlock);
		device_release(sc->sc_dev);
		if (error)
			break;

		dkwl->dkwl_ncopied++;
	}
	dkwl->dkwl_nwedges = pdk->dk_nwedges;
	mutex_exit(&pdk->dk_openlock);

	return error;
}

static device_t
dkwedge_find_by_wname_acquire(const char *wname)
{
	device_t dv = NULL;
	struct dkwedge_softc *sc;
	int i;

	rw_enter(&dkwedges_lock, RW_READER);
	for (i = 0; i < ndkwedges; i++) {
		if ((sc = dkwedges[i]) == NULL || sc->sc_dev == NULL)
			continue;
		if (strcmp(sc->sc_wname, wname) == 0) {
			if (dv != NULL) {
				printf(
				    "WARNING: double match for wedge name %s "
				    "(%s, %s)\n", wname, device_xname(dv),
				    device_xname(sc->sc_dev));
				continue;
			}
			device_acquire(sc->sc_dev);
			dv = sc->sc_dev;
		}
	}
	rw_exit(&dkwedges_lock);
	return dv;
}

static device_t
dkwedge_find_by_parent_acquire(const char *name, size_t *i)
{

	rw_enter(&dkwedges_lock, RW_READER);
	for (; *i < (size_t)ndkwedges; (*i)++) {
		struct dkwedge_softc *sc;
		if ((sc = dkwedges[*i]) == NULL || sc->sc_dev == NULL)
			continue;
		if (strcmp(sc->sc_parent->dk_name, name) != 0)
			continue;
		device_acquire(sc->sc_dev);
		rw_exit(&dkwedges_lock);
		return sc->sc_dev;
	}
	rw_exit(&dkwedges_lock);
	return NULL;
}

/* XXX unsafe */
device_t
dkwedge_find_by_wname(const char *wname)
{
	device_t dv;

	if ((dv = dkwedge_find_by_wname_acquire(wname)) == NULL)
		return NULL;
	device_release(dv);
	return dv;
}

/* XXX unsafe */
device_t
dkwedge_find_by_parent(const char *name, size_t *i)
{
	device_t dv;

	if ((dv = dkwedge_find_by_parent_acquire(name, i)) == NULL)
		return NULL;
	device_release(dv);
	return dv;
}

void
dkwedge_print_wnames(void)
{
	struct dkwedge_softc *sc;
	int i;

	rw_enter(&dkwedges_lock, RW_READER);
	for (i = 0; i < ndkwedges; i++) {
		if ((sc = dkwedges[i]) == NULL || sc->sc_dev == NULL)
			continue;
		printf(" wedge:%s", sc->sc_wname);
	}
	rw_exit(&dkwedges_lock);
}

/*
 * We need a dummy object to stuff into the dkwedge discovery method link
 * set to ensure that there is always at least one object in the set.
 */
static struct dkwedge_discovery_method dummy_discovery_method;
__link_set_add_bss(dkwedge_methods, dummy_discovery_method);

/*
 * dkwedge_init:
 *
 *	Initialize the disk wedge subsystem.
 */
void
dkwedge_init(void)
{
	__link_set_decl(dkwedge_methods, struct dkwedge_discovery_method);
	struct dkwedge_discovery_method * const *ddmp;
	struct dkwedge_discovery_method *lddm, *ddm;

	rw_init(&dkwedges_lock);
	rw_init(&dkwedge_discovery_methods_lock);

	if (config_cfdriver_attach(&dk_cd) != 0)
		panic("dkwedge: unable to attach cfdriver");
	if (config_cfattach_attach(dk_cd.cd_name, &dk_ca) != 0)
		panic("dkwedge: unable to attach cfattach");

	rw_enter(&dkwedge_discovery_methods_lock, RW_WRITER);

	LIST_INIT(&dkwedge_discovery_methods);

	__link_set_foreach(ddmp, dkwedge_methods) {
		ddm = *ddmp;
		if (ddm == &dummy_discovery_method)
			continue;
		if (LIST_EMPTY(&dkwedge_discovery_methods)) {
			LIST_INSERT_HEAD(&dkwedge_discovery_methods,
			    ddm, ddm_list);
			continue;
		}
		LIST_FOREACH(lddm, &dkwedge_discovery_methods, ddm_list) {
			if (ddm->ddm_priority == lddm->ddm_priority) {
				aprint_error("dk-method-%s: method \"%s\" "
				    "already exists at priority %d\n",
				    ddm->ddm_name, lddm->ddm_name,
				    lddm->ddm_priority);
				/* Not inserted. */
				break;
			}
			if (ddm->ddm_priority < lddm->ddm_priority) {
				/* Higher priority; insert before. */
				LIST_INSERT_BEFORE(lddm, ddm, ddm_list);
				break;
			}
			if (LIST_NEXT(lddm, ddm_list) == NULL) {
				/* Last one; insert after. */
				KASSERT(lddm->ddm_priority < ddm->ddm_priority);
				LIST_INSERT_AFTER(lddm, ddm, ddm_list);
				break;
			}
		}
	}

	rw_exit(&dkwedge_discovery_methods_lock);
}

#ifdef DKWEDGE_AUTODISCOVER
int	dkwedge_autodiscover = 1;
#else
int	dkwedge_autodiscover = 0;
#endif

/*
 * dkwedge_discover:	[exported function]
 *
 *	Discover the wedges on a newly attached disk.
 *	Remove all unused wedges on the disk first.
 */
void
dkwedge_discover(struct disk *pdk)
{
	struct dkwedge_discovery_method *ddm;
	struct vnode *vp;
	int error;
	dev_t pdev;

	/*
	 * Require people playing with wedges to enable this explicitly.
	 */
	if (dkwedge_autodiscover == 0)
		return;

	rw_enter(&dkwedge_discovery_methods_lock, RW_READER);

	/*
	 * Use the character device for scanning, the block device
	 * is busy if there are already wedges attached.
	 */
	error = dkwedge_compute_pdev(pdk->dk_name, &pdev, VCHR);
	if (error) {
		aprint_error("%s: unable to compute pdev, error = %d\n",
		    pdk->dk_name, error);
		goto out;
	}

	error = cdevvp(pdev, &vp);
	if (error) {
		aprint_error("%s: unable to find vnode for pdev, error = %d\n",
		    pdk->dk_name, error);
		goto out;
	}

	error = vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if (error) {
		aprint_error("%s: unable to lock vnode for pdev, error = %d\n",
		    pdk->dk_name, error);
		vrele(vp);
		goto out;
	}

	error = VOP_OPEN(vp, FREAD | FSILENT, NOCRED);
	if (error) {
		if (error != ENXIO)
			aprint_error("%s: unable to open device, error = %d\n",
			    pdk->dk_name, error);
		vput(vp);
		goto out;
	}
	VOP_UNLOCK(vp);

	/*
	 * Remove unused wedges
	 */
	dkwedge_delidle(pdk);

	/*
	 * For each supported partition map type, look to see if
	 * this map type exists.  If so, parse it and add the
	 * corresponding wedges.
	 */
	LIST_FOREACH(ddm, &dkwedge_discovery_methods, ddm_list) {
		error = (*ddm->ddm_discover)(pdk, vp);
		if (error == 0) {
			/* Successfully created wedges; we're done. */
			break;
		}
	}

	error = vn_close(vp, FREAD, NOCRED);
	if (error) {
		aprint_error("%s: unable to close device, error = %d\n",
		    pdk->dk_name, error);
		/* We'll just assume the vnode has been cleaned up. */
	}

out:
	rw_exit(&dkwedge_discovery_methods_lock);
}

/*
 * dkwedge_read:
 *
 *	Read some data from the specified disk, used for
 *	partition discovery.
 */
int
dkwedge_read(struct disk *pdk, struct vnode *vp, daddr_t blkno,
    void *tbuf, size_t len)
{
	buf_t *bp;
	int error;
	bool isopen;
	dev_t bdev;
	struct vnode *bdvp;

	/*
	 * The kernel cannot read from a character device vnode
	 * as physio() only handles user memory.
	 *
	 * If the block device has already been opened by a wedge
	 * use that vnode and temporarily bump the open counter.
	 *
	 * Otherwise try to open the block device.
	 */

	bdev = devsw_chr2blk(vp->v_rdev);

	mutex_enter(&pdk->dk_rawlock);
	if (pdk->dk_rawopens != 0) {
		KASSERT(pdk->dk_rawvp != NULL);
		isopen = true;
		++pdk->dk_rawopens;
		bdvp = pdk->dk_rawvp;
		error = 0;
	} else {
		isopen = false;
		error = dk_open_parent(bdev, FREAD, &bdvp);
	}
	mutex_exit(&pdk->dk_rawlock);

	if (error)
		return error;

	bp = getiobuf(bdvp, true);
	bp->b_flags = B_READ;
	bp->b_cflags = BC_BUSY;
	bp->b_dev = bdev;
	bp->b_data = tbuf;
	bp->b_bufsize = bp->b_bcount = len;
	bp->b_blkno = blkno;
	bp->b_cylinder = 0;
	bp->b_error = 0;

	VOP_STRATEGY(bdvp, bp);
	error = biowait(bp);
	putiobuf(bp);

	mutex_enter(&pdk->dk_rawlock);
	if (isopen) {
		--pdk->dk_rawopens;
	} else {
		dk_close_parent(bdvp, FREAD);
	}
	mutex_exit(&pdk->dk_rawlock);

	return error;
}

/*
 * dkwedge_lookup:
 *
 *	Look up a dkwedge_softc based on the provided dev_t.
 *
 *	Caller must guarantee the wedge is referenced.
 */
static struct dkwedge_softc *
dkwedge_lookup(dev_t dev)
{

	return device_lookup_private(&dk_cd, minor(dev));
}

static struct dkwedge_softc *
dkwedge_lookup_acquire(dev_t dev)
{
	device_t dv = device_lookup_acquire(&dk_cd, minor(dev));

	if (dv == NULL)
		return NULL;
	return device_private(dv);
}

static int
dk_open_parent(dev_t dev, int mode, struct vnode **vpp)
{
	struct vnode *vp;
	int error;

	error = bdevvp(dev, &vp);
	if (error)
		return error;

	error = vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if (error) {
		vrele(vp);
		return error;
	}
	error = VOP_OPEN(vp, mode, NOCRED);
	if (error) {
		vput(vp);
		return error;
	}

	/* VOP_OPEN() doesn't do this for us. */
	if (mode & FWRITE) {
		mutex_enter(vp->v_interlock);
		vp->v_writecount++;
		mutex_exit(vp->v_interlock);
	}

	VOP_UNLOCK(vp);

	*vpp = vp;

	return 0;
}

static int
dk_close_parent(struct vnode *vp, int mode)
{
	int error;

	error = vn_close(vp, mode, NOCRED);
	return error;
}

/*
 * dkopen:		[devsw entry point]
 *
 *	Open a wedge.
 */
static int
dkopen(dev_t dev, int flags, int fmt, struct lwp *l)
{
	struct dkwedge_softc *sc = dkwedge_lookup(dev);
	int error = 0;

	if (sc == NULL)
		return ENXIO;
	KASSERT(sc->sc_dev != NULL);
	KASSERT(sc->sc_state == DKW_STATE_RUNNING);

	/*
	 * We go through a complicated little dance to only open the parent
	 * vnode once per wedge, no matter how many times the wedge is
	 * opened.  The reason?  We see one dkopen() per open call, but
	 * only dkclose() on the last close.
	 */
	mutex_enter(&sc->sc_dk.dk_openlock);
	mutex_enter(&sc->sc_parent->dk_rawlock);
	if (sc->sc_dk.dk_openmask == 0) {
		error = dkfirstopen(sc, flags);
		if (error)
			goto out;
	} else if (flags & ~sc->sc_mode & FWRITE) {
		/*
		 * The parent is already open, but the previous attempt
		 * to open it read/write failed and fell back to
		 * read-only.  In that case, we assume the medium is
		 * read-only and fail to open the wedge read/write.
		 */
		error = EROFS;
		goto out;
	}
	KASSERT(sc->sc_mode != 0);
	KASSERTMSG(sc->sc_mode & FREAD, "%s: sc_mode=%x",
	    device_xname(sc->sc_dev), sc->sc_mode);
	KASSERTMSG((flags & FWRITE) ? (sc->sc_mode & FWRITE) : 1,
	    "%s: flags=%x sc_mode=%x",
	    device_xname(sc->sc_dev), flags, sc->sc_mode);
	if (fmt == S_IFCHR)
		sc->sc_dk.dk_copenmask |= 1;
	else
		sc->sc_dk.dk_bopenmask |= 1;
	sc->sc_dk.dk_openmask =
	    sc->sc_dk.dk_copenmask | sc->sc_dk.dk_bopenmask;

out:	mutex_exit(&sc->sc_parent->dk_rawlock);
	mutex_exit(&sc->sc_dk.dk_openlock);
	return error;
}

static int
dkfirstopen(struct dkwedge_softc *sc, int flags)
{
	struct dkwedge_softc *nsc;
	struct vnode *vp;
	int mode;
	int error;

	KASSERT(mutex_owned(&sc->sc_dk.dk_openlock));
	KASSERT(mutex_owned(&sc->sc_parent->dk_rawlock));

	if (sc->sc_parent->dk_rawopens == 0) {
		KASSERT(sc->sc_parent->dk_rawvp == NULL);
		/*
		 * Try open read-write. If this fails for EROFS
		 * and wedge is read-only, retry to open read-only.
		 */
		mode = FREAD | FWRITE;
		error = dk_open_parent(sc->sc_pdev, mode, &vp);
		if (error == EROFS && (flags & FWRITE) == 0) {
			mode &= ~FWRITE;
			error = dk_open_parent(sc->sc_pdev, mode, &vp);
		}
		if (error)
			return error;
		KASSERT(vp != NULL);
		sc->sc_parent->dk_rawvp = vp;
	} else {
		/*
		 * Retrieve mode from an already opened wedge.
		 *
		 * At this point, dk_rawopens is bounded by the number
		 * of dkwedge devices in the system, which is limited
		 * by autoconf device numbering to INT_MAX.  Since
		 * dk_rawopens is unsigned, this can't overflow.
		 */
		KASSERT(sc->sc_parent->dk_rawopens < UINT_MAX);
		KASSERT(sc->sc_parent->dk_rawvp != NULL);
		mode = 0;
		mutex_enter(&sc->sc_parent->dk_openlock);
		LIST_FOREACH(nsc, &sc->sc_parent->dk_wedges, sc_plink) {
			if (nsc == sc || nsc->sc_dk.dk_openmask == 0)
				continue;
			mode = nsc->sc_mode;
			break;
		}
		mutex_exit(&sc->sc_parent->dk_openlock);
	}
	sc->sc_mode = mode;
	sc->sc_parent->dk_rawopens++;

	return 0;
}

static void
dklastclose(struct dkwedge_softc *sc)
{

	KASSERT(mutex_owned(&sc->sc_dk.dk_openlock));
	KASSERT(mutex_owned(&sc->sc_parent->dk_rawlock));
	KASSERT(sc->sc_parent->dk_rawopens > 0);
	KASSERT(sc->sc_parent->dk_rawvp != NULL);

	if (--sc->sc_parent->dk_rawopens == 0) {
		struct vnode *const vp = sc->sc_parent->dk_rawvp;
		const int mode = sc->sc_mode;

		sc->sc_parent->dk_rawvp = NULL;
		sc->sc_mode = 0;

		dk_close_parent(vp, mode);
	}
}

/*
 * dkclose:		[devsw entry point]
 *
 *	Close a wedge.
 */
static int
dkclose(dev_t dev, int flags, int fmt, struct lwp *l)
{
	struct dkwedge_softc *sc = dkwedge_lookup(dev);

	/*
	 * dkclose can be called even if dkopen didn't succeed, so we
	 * have to handle the same possibility that the wedge may not
	 * exist.
	 */
	if (sc == NULL)
		return ENXIO;
	KASSERT(sc->sc_dev != NULL);
	KASSERT(sc->sc_state != DKW_STATE_LARVAL);
	KASSERT(sc->sc_state != DKW_STATE_DEAD);

	mutex_enter(&sc->sc_dk.dk_openlock);
	mutex_enter(&sc->sc_parent->dk_rawlock);

	KASSERT(sc->sc_dk.dk_openmask != 0);

	if (fmt == S_IFCHR)
		sc->sc_dk.dk_copenmask &= ~1;
	else
		sc->sc_dk.dk_bopenmask &= ~1;
	sc->sc_dk.dk_openmask =
	    sc->sc_dk.dk_copenmask | sc->sc_dk.dk_bopenmask;

	if (sc->sc_dk.dk_openmask == 0) {
		dklastclose(sc);
	}

	mutex_exit(&sc->sc_parent->dk_rawlock);
	mutex_exit(&sc->sc_dk.dk_openlock);

	return 0;
}

/*
 * dkcancel:		[devsw entry point]
 *
 *	Cancel any pending I/O operations waiting on a wedge.
 */
static int
dkcancel(dev_t dev, int flags, int fmt, struct lwp *l)
{
	struct dkwedge_softc *sc = dkwedge_lookup(dev);

	KASSERT(sc != NULL);
	KASSERT(sc->sc_dev != NULL);
	KASSERT(sc->sc_state != DKW_STATE_LARVAL);
	KASSERT(sc->sc_state != DKW_STATE_DEAD);

	/*
	 * Disk I/O is expected to complete or fail within a reasonable
	 * timeframe -- it's storage, not communication.  Further, the
	 * character and block device interface guarantees that prior
	 * reads and writes have completed or failed by the time close
	 * returns -- we are not to cancel them here.  If the parent
	 * device's hardware is gone, the parent driver can make them
	 * fail.  Nothing for dk(4) itself to do.
	 */

	return 0;
}

/*
 * dkstrategy:		[devsw entry point]
 *
 *	Perform I/O based on the wedge I/O strategy.
 */
static void
dkstrategy(struct buf *bp)
{
	struct dkwedge_softc *sc = dkwedge_lookup(bp->b_dev);
	uint64_t p_size, p_offset;

	KASSERT(sc != NULL);
	KASSERT(sc->sc_dev != NULL);
	KASSERT(sc->sc_state != DKW_STATE_LARVAL);
	KASSERT(sc->sc_state != DKW_STATE_DEAD);
	KASSERT(sc->sc_parent->dk_rawvp != NULL);

	/* If it's an empty transfer, wake up the top half now. */
	if (bp->b_bcount == 0)
		goto done;

	p_offset = sc->sc_offset << sc->sc_parent->dk_blkshift;
	p_size = dkwedge_size(sc) << sc->sc_parent->dk_blkshift;

	/* Make sure it's in-range. */
	if (bounds_check_with_mediasize(bp, DEV_BSIZE, p_size) <= 0)
		goto done;

	/* Translate it to the parent's raw LBA. */
	bp->b_rawblkno = bp->b_blkno + p_offset;

	/* Place it in the queue and start I/O on the unit. */
	mutex_enter(&sc->sc_iolock);
	disk_wait(&sc->sc_dk);
	bufq_put(sc->sc_bufq, bp);
	mutex_exit(&sc->sc_iolock);

	dkstart(sc);
	return;

done:
	bp->b_resid = bp->b_bcount;
	biodone(bp);
}

/*
 * dkstart:
 *
 *	Start I/O that has been enqueued on the wedge.
 */
static void
dkstart(struct dkwedge_softc *sc)
{
	struct vnode *vp;
	struct buf *bp, *nbp;

	mutex_enter(&sc->sc_iolock);

	/* Do as much work as has been enqueued. */
	while ((bp = bufq_peek(sc->sc_bufq)) != NULL) {
		if (sc->sc_iostop) {
			(void) bufq_get(sc->sc_bufq);
			mutex_exit(&sc->sc_iolock);
			bp->b_error = ENXIO;
			bp->b_resid = bp->b_bcount;
			biodone(bp);
			mutex_enter(&sc->sc_iolock);
			continue;
		}

		/* fetch an I/O buf with sc_iolock dropped */
		mutex_exit(&sc->sc_iolock);
		nbp = getiobuf(sc->sc_parent->dk_rawvp, false);
		mutex_enter(&sc->sc_iolock);
		if (nbp == NULL) {
			/*
			 * No resources to run this request; leave the
			 * buffer queued up, and schedule a timer to
			 * restart the queue in 1/2 a second.
			 */
			if (!sc->sc_iostop)
				callout_schedule(&sc->sc_restart_ch, hz/2);
			break;
		}

		/*
		 * fetch buf, this can fail if another thread
		 * has already processed the queue, it can also
		 * return a completely different buf.
		 */
		bp = bufq_get(sc->sc_bufq);
		if (bp == NULL) {
			mutex_exit(&sc->sc_iolock);
			putiobuf(nbp);
			mutex_enter(&sc->sc_iolock);
			continue;
		}

		/* Instrumentation. */
		disk_busy(&sc->sc_dk);

		/* release lock for VOP_STRATEGY */
		mutex_exit(&sc->sc_iolock);

		nbp->b_data = bp->b_data;
		nbp->b_flags = bp->b_flags;
		nbp->b_oflags = bp->b_oflags;
		nbp->b_cflags = bp->b_cflags;
		nbp->b_iodone = dkiodone;
		nbp->b_proc = bp->b_proc;
		nbp->b_blkno = bp->b_rawblkno;
		nbp->b_dev = sc->sc_parent->dk_rawvp->v_rdev;
		nbp->b_bcount = bp->b_bcount;
		nbp->b_private = bp;
		BIO_COPYPRIO(nbp, bp);

		vp = nbp->b_vp;
		if ((nbp->b_flags & B_READ) == 0) {
			mutex_enter(vp->v_interlock);
			vp->v_numoutput++;
			mutex_exit(vp->v_interlock);
		}
		VOP_STRATEGY(vp, nbp);

		mutex_enter(&sc->sc_iolock);
	}

	mutex_exit(&sc->sc_iolock);
}

/*
 * dkiodone:
 *
 *	I/O to a wedge has completed; alert the top half.
 */
static void
dkiodone(struct buf *bp)
{
	struct buf *obp = bp->b_private;
	struct dkwedge_softc *sc = dkwedge_lookup(obp->b_dev);

	KASSERT(sc != NULL);
	KASSERT(sc->sc_dev != NULL);

	if (bp->b_error != 0)
		obp->b_error = bp->b_error;
	obp->b_resid = bp->b_resid;
	putiobuf(bp);

	mutex_enter(&sc->sc_iolock);
	disk_unbusy(&sc->sc_dk, obp->b_bcount - obp->b_resid,
	    obp->b_flags & B_READ);
	mutex_exit(&sc->sc_iolock);

	biodone(obp);

	/* Kick the queue in case there is more work we can do. */
	dkstart(sc);
}

/*
 * dkrestart:
 *
 *	Restart the work queue after it was stalled due to
 *	a resource shortage.  Invoked via a callout.
 */
static void
dkrestart(void *v)
{
	struct dkwedge_softc *sc = v;

	dkstart(sc);
}

/*
 * dkminphys:
 *
 *	Call parent's minphys function.
 */
static void
dkminphys(struct buf *bp)
{
	struct dkwedge_softc *sc = dkwedge_lookup(bp->b_dev);
	dev_t dev;

	KASSERT(sc != NULL);
	KASSERT(sc->sc_dev != NULL);

	dev = bp->b_dev;
	bp->b_dev = sc->sc_pdev;
	if (sc->sc_parent->dk_driver && sc->sc_parent->dk_driver->d_minphys)
		(*sc->sc_parent->dk_driver->d_minphys)(bp);
	else
		minphys(bp);
	bp->b_dev = dev;
}

/*
 * dkread:		[devsw entry point]
 *
 *	Read from a wedge.
 */
static int
dkread(dev_t dev, struct uio *uio, int flags)
{
	struct dkwedge_softc *sc __diagused = dkwedge_lookup(dev);

	KASSERT(sc != NULL);
	KASSERT(sc->sc_dev != NULL);
	KASSERT(sc->sc_state != DKW_STATE_LARVAL);
	KASSERT(sc->sc_state != DKW_STATE_DEAD);

	return physio(dkstrategy, NULL, dev, B_READ, dkminphys, uio);
}

/*
 * dkwrite:		[devsw entry point]
 *
 *	Write to a wedge.
 */
static int
dkwrite(dev_t dev, struct uio *uio, int flags)
{
	struct dkwedge_softc *sc __diagused = dkwedge_lookup(dev);

	KASSERT(sc != NULL);
	KASSERT(sc->sc_dev != NULL);
	KASSERT(sc->sc_state != DKW_STATE_LARVAL);
	KASSERT(sc->sc_state != DKW_STATE_DEAD);

	return physio(dkstrategy, NULL, dev, B_WRITE, dkminphys, uio);
}

/*
 * dkioctl:		[devsw entry point]
 *
 *	Perform an ioctl request on a wedge.
 */
static int
dkioctl(dev_t dev, u_long cmd, void *data, int flag, struct lwp *l)
{
	struct dkwedge_softc *sc = dkwedge_lookup(dev);
	int error = 0;

	KASSERT(sc != NULL);
	KASSERT(sc->sc_dev != NULL);
	KASSERT(sc->sc_state != DKW_STATE_LARVAL);
	KASSERT(sc->sc_state != DKW_STATE_DEAD);
	KASSERT(sc->sc_parent->dk_rawvp != NULL);

	/*
	 * We pass NODEV instead of our device to indicate we don't
	 * want to handle disklabel ioctls
	 */
	error = disk_ioctl(&sc->sc_dk, NODEV, cmd, data, flag, l);
	if (error != EPASSTHROUGH)
		return error;

	error = 0;

	switch (cmd) {
	case DIOCGSTRATEGY:
	case DIOCGCACHE:
	case DIOCCACHESYNC:
		error = VOP_IOCTL(sc->sc_parent->dk_rawvp, cmd, data, flag,
		    l != NULL ? l->l_cred : NOCRED);
		break;
	case DIOCGWEDGEINFO: {
		struct dkwedge_info *dkw = data;

		strlcpy(dkw->dkw_devname, device_xname(sc->sc_dev),
		    sizeof(dkw->dkw_devname));
	    	memcpy(dkw->dkw_wname, sc->sc_wname, sizeof(dkw->dkw_wname));
		dkw->dkw_wname[sizeof(dkw->dkw_wname) - 1] = '\0';
		strlcpy(dkw->dkw_parent, sc->sc_parent->dk_name,
		    sizeof(dkw->dkw_parent));
		dkw->dkw_offset = sc->sc_offset;
		dkw->dkw_size = dkwedge_size(sc);
		strlcpy(dkw->dkw_ptype, sc->sc_ptype, sizeof(dkw->dkw_ptype));

		break;
	}
	case DIOCGSECTORALIGN: {
		struct disk_sectoralign *dsa = data;
		uint32_t r;

		error = VOP_IOCTL(sc->sc_parent->dk_rawvp, cmd, dsa, flag,
		    l != NULL ? l->l_cred : NOCRED);
		if (error)
			break;

		r = sc->sc_offset % dsa->dsa_alignment;
		if (r < dsa->dsa_firstaligned)
			dsa->dsa_firstaligned = dsa->dsa_firstaligned - r;
		else
			dsa->dsa_firstaligned = (dsa->dsa_firstaligned +
			    dsa->dsa_alignment) - r;
		dsa->dsa_firstaligned %= dsa->dsa_alignment;
		break;
	}
	default:
		error = ENOTTY;
	}

	return error;
}

/*
 * dkdiscard:		[devsw entry point]
 *
 *	Perform a discard-range request on a wedge.
 */
static int
dkdiscard(dev_t dev, off_t pos, off_t len)
{
	struct dkwedge_softc *sc = dkwedge_lookup(dev);
	uint64_t size = dkwedge_size(sc);
	unsigned shift;
	off_t offset, maxlen;
	int error;

	KASSERT(sc != NULL);
	KASSERT(sc->sc_dev != NULL);
	KASSERT(sc->sc_state != DKW_STATE_LARVAL);
	KASSERT(sc->sc_state != DKW_STATE_DEAD);
	KASSERT(sc->sc_parent->dk_rawvp != NULL);

	/* XXX check bounds on size/offset up front */
	shift = (sc->sc_parent->dk_blkshift + DEV_BSHIFT);
	KASSERT(__type_fit(off_t, size));
	KASSERT(__type_fit(off_t, sc->sc_offset));
	KASSERT(0 <= sc->sc_offset);
	KASSERT(size <= (__type_max(off_t) >> shift));
	KASSERT(sc->sc_offset <= ((__type_max(off_t) >> shift) - size));
	offset = ((off_t)sc->sc_offset << shift);
	maxlen = ((off_t)size << shift);

	if (len > maxlen)
		return EINVAL;
	if (pos > (maxlen - len))
		return EINVAL;

	pos += offset;

	vn_lock(sc->sc_parent->dk_rawvp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_FDISCARD(sc->sc_parent->dk_rawvp, pos, len);
	VOP_UNLOCK(sc->sc_parent->dk_rawvp);

	return error;
}

/*
 * dksize:		[devsw entry point]
 *
 *	Query the size of a wedge for the purpose of performing a dump
 *	or for swapping to.
 */
static int
dksize(dev_t dev)
{
	/*
	 * Don't bother taking a reference because this is only used
	 * either (a) while the device is open (for swap), or (b) while
	 * any multiprocessing is quiescent (for crash dumps).
	 */
	struct dkwedge_softc *sc = dkwedge_lookup(dev);
	uint64_t p_size;
	int rv = -1;

	if (sc == NULL)
		return -1;
	if (sc->sc_state != DKW_STATE_RUNNING)
		return -1;

	/* Our content type is static, no need to open the device. */

	p_size = dkwedge_size(sc) << sc->sc_parent->dk_blkshift;
	if (strcmp(sc->sc_ptype, DKW_PTYPE_SWAP) == 0) {
		/* Saturate if we are larger than INT_MAX. */
		if (p_size > INT_MAX)
			rv = INT_MAX;
		else
			rv = (int)p_size;
	}

	return rv;
}

/*
 * dkdump:		[devsw entry point]
 *
 *	Perform a crash dump to a wedge.
 */
static int
dkdump(dev_t dev, daddr_t blkno, void *va, size_t size)
{
	/*
	 * Don't bother taking a reference because this is only used
	 * while any multiprocessing is quiescent.
	 */
	struct dkwedge_softc *sc = dkwedge_lookup(dev);
	const struct bdevsw *bdev;
	uint64_t p_size, p_offset;

	if (sc == NULL)
		return ENXIO;
	if (sc->sc_state != DKW_STATE_RUNNING)
		return ENXIO;

	/* Our content type is static, no need to open the device. */

	if (strcmp(sc->sc_ptype, DKW_PTYPE_SWAP) != 0 &&
	    strcmp(sc->sc_ptype, DKW_PTYPE_RAID) != 0 &&
	    strcmp(sc->sc_ptype, DKW_PTYPE_CGD) != 0)
		return ENXIO;
	if (size % DEV_BSIZE != 0)
		return EINVAL;

	p_offset = sc->sc_offset << sc->sc_parent->dk_blkshift;
	p_size = dkwedge_size(sc) << sc->sc_parent->dk_blkshift;

	if (blkno < 0 || blkno + size/DEV_BSIZE > p_size) {
		printf("%s: blkno (%" PRIu64 ") + size / DEV_BSIZE (%zu) > "
		    "p_size (%" PRIu64 ")\n", __func__, blkno,
		    size/DEV_BSIZE, p_size);
		return EINVAL;
	}

	bdev = bdevsw_lookup(sc->sc_pdev);
	return (*bdev->d_dump)(sc->sc_pdev, blkno + p_offset, va, size);
}

/*
 * config glue
 */

/*
 * dkwedge_find_partition
 *
 *	Find wedge corresponding to the specified parent name
 *	and offset/length.
 */
static device_t
dkwedge_find_partition_acquire(device_t parent, daddr_t startblk,
    uint64_t nblks)
{
	struct dkwedge_softc *sc;
	int i;
	device_t wedge = NULL;

	rw_enter(&dkwedges_lock, RW_READER);
	for (i = 0; i < ndkwedges; i++) {
		if ((sc = dkwedges[i]) == NULL || sc->sc_dev == NULL)
			continue;
		if (strcmp(sc->sc_parent->dk_name, device_xname(parent)) == 0 &&
		    sc->sc_offset == startblk &&
		    dkwedge_size(sc) == nblks) {
			if (wedge) {
				printf("WARNING: double match for boot wedge "
				    "(%s, %s)\n",
				    device_xname(wedge),
				    device_xname(sc->sc_dev));
				continue;
			}
			wedge = sc->sc_dev;
			device_acquire(wedge);
		}
	}
	rw_exit(&dkwedges_lock);

	return wedge;
}

/* XXX unsafe */
device_t
dkwedge_find_partition(device_t parent, daddr_t startblk,
    uint64_t nblks)
{
	device_t dv;

	if ((dv = dkwedge_find_partition_acquire(parent, startblk, nblks))
	    == NULL)
		return NULL;
	device_release(dv);
	return dv;
}

const char *
dkwedge_get_parent_name(dev_t dev)
{
	/* XXX: perhaps do this in lookup? */
	int bmaj = bdevsw_lookup_major(&dk_bdevsw);
	int cmaj = cdevsw_lookup_major(&dk_cdevsw);

	if (major(dev) != bmaj && major(dev) != cmaj)
		return NULL;

	struct dkwedge_softc *const sc = dkwedge_lookup_acquire(dev);
	if (sc == NULL)
		return NULL;
	const char *const name = sc->sc_parent->dk_name;
	device_release(sc->sc_dev);
	return name;
}
