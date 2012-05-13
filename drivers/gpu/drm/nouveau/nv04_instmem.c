/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include "drmP.h"

#include "nouveau_drv.h"
#include "nouveau_instmem.h"
#include "nouveau_gpuobj.h"

struct nv04_instmem_priv {
	struct nouveau_instmem base;
	struct drm_mm heap;
	spinlock_t lock;
};

static int
nv04_instmem_get(struct nouveau_instmem *pimem, struct nouveau_gpuobj *gpuobj,
		 struct nouveau_vm *vm, u32 size, u32 align)
{
	struct nv04_instmem_priv *priv = (void *)pimem;
	struct drm_mm_node *ramin = NULL;

	do {
		if (drm_mm_pre_get(&priv->heap))
			return -ENOMEM;

		spin_lock(&priv->lock);
		ramin = drm_mm_search_free(&priv->heap, size, align, 0);
		if (ramin == NULL) {
			spin_unlock(&priv->lock);
			return -ENOMEM;
		}

		ramin = drm_mm_get_block_atomic(ramin, size, align);
		spin_unlock(&priv->lock);
	} while (ramin == NULL);

	gpuobj->node  = ramin;
	gpuobj->vinst = ramin->start;
	return 0;
}

static void
nv04_instmem_put(struct nouveau_instmem *pimem, struct nouveau_gpuobj *gpuobj)
{
	struct nv04_instmem_priv *priv = (void *)pimem;

	spin_lock(&priv->lock);
	drm_mm_put_block(gpuobj->node);
	gpuobj->node = NULL;
	spin_unlock(&priv->lock);
}

static int
nv04_instmem_map(struct nouveau_instmem *pimem, struct nouveau_gpuobj *gpuobj)
{
	gpuobj->pinst = gpuobj->vinst;
	return 0;
}

static void
nv04_instmem_unmap(struct nouveau_instmem *pimem, struct nouveau_gpuobj *gpuobj)
{
}

static void
nv04_instmem_flush(struct nouveau_instmem *pimem)
{
}

static void
nv04_instmem_destroy(struct nouveau_device *ndev, int subdev)
{
	struct nv04_instmem_priv *priv = nv_subdev(ndev, subdev);
	if (drm_mm_initialized(&priv->heap))
		drm_mm_takedown(&priv->heap);
}

int
nv04_instmem_create(struct nouveau_device *ndev, int subdev)
{
	struct nv04_instmem_priv *priv;
	u32 offset;
	int ret;

	ret = nouveau_subdev_create(ndev, subdev, "INSTMEM", "instmem", &priv);
	if (ret)
		return ret;

	priv->base.base.device = ndev;
	priv->base.base.destroy = nv04_instmem_destroy;
	priv->base.get = nv04_instmem_get;
	priv->base.put = nv04_instmem_put;
	priv->base.map = nv04_instmem_map;
	priv->base.unmap = nv04_instmem_unmap;
	priv->base.flush = nv04_instmem_flush;
	spin_lock_init(&priv->lock);

	/* Reserve space at end of VRAM for PRAMIN */
	if (ndev->card_type >= NV_40) {
		u32 vs = hweight8((nv_rd32(ndev, 0x001540) & 0x0000ff00) >> 8);
		u32 rsvd;

		/* estimate grctx size, the magics come from nv40_grctx.c */
		if      (ndev->chipset == 0x40) rsvd = 0x6aa0 * vs;
		else if (ndev->chipset  < 0x43) rsvd = 0x4f00 * vs;
		else if (nv44_graph_class(ndev))	    rsvd = 0x4980 * vs;
		else				    rsvd = 0x4a40 * vs;
		rsvd += 16 * 1024;
		rsvd *= 32; /* per-channel */

		rsvd += 512 * 1024; /* pci(e)gart table */
		rsvd += 512 * 1024; /* object storage */

		ndev->ramin_rsvd_vram = round_up(rsvd, 4096);
	} else {
		ndev->ramin_rsvd_vram = 512 * 1024;
	}

	/* It appears RAMRO (or something?) is controlled by 0x2220/0x2230
	 * on certain NV4x chipsets as well as RAMFC.  When 0x2230 == 0
	 * ("new style" control) the upper 16-bits of 0x2220 points at this
	 * other mysterious table that's clobbering important things.
	 *
	 * We're now pointing this at RAMIN+0x30000 to avoid RAMFC getting
	 * smashed to pieces on us, so reserve 0x30000-0x40000 too..
	 */
	if (ndev->card_type < NV_40)
		offset = 0x22800;
	else
		offset = 0x40000;

	ret = drm_mm_init(&priv->heap, offset, ndev->ramin_rsvd_vram - offset);
	if (ret)
		goto done;

done:
	return nouveau_subdev_init(ndev, subdev, ret);
}
