#include "drmP.h"
#include "drm.h"

#include "nouveau_drv.h"

int nv04_instmem_init(struct nouveau_device *ndev)
{
	u32 offset;
	int ret;

	/* RAMIN always available */
	ndev->ramin_available = true;

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

	ret = drm_mm_init(&ndev->ramin_heap, offset,
			  ndev->ramin_rsvd_vram - offset);
	if (ret) {
		NV_ERROR(ndev, "Failed to init RAMIN heap: %d\n", ret);
		return ret;
	}

	return 0;
}

void
nv04_instmem_takedown(struct nouveau_device *ndev)
{
	if (drm_mm_initialized(&ndev->ramin_heap))
		drm_mm_takedown(&ndev->ramin_heap);
}

int
nv04_instmem_suspend(struct nouveau_device *ndev)
{
	return 0;
}

void
nv04_instmem_resume(struct nouveau_device *ndev)
{
}

int
nv04_instmem_get(struct nouveau_gpuobj *gpuobj, struct nouveau_channel *chan,
		 u32 size, u32 align)
{
	struct nouveau_device *ndev = gpuobj->device;
	struct drm_mm_node *ramin = NULL;

	do {
		if (drm_mm_pre_get(&ndev->ramin_heap))
			return -ENOMEM;

		spin_lock(&ndev->ramin_lock);
		ramin = drm_mm_search_free(&ndev->ramin_heap, size, align, 0);
		if (ramin == NULL) {
			spin_unlock(&ndev->ramin_lock);
			return -ENOMEM;
		}

		ramin = drm_mm_get_block_atomic(ramin, size, align);
		spin_unlock(&ndev->ramin_lock);
	} while (ramin == NULL);

	gpuobj->node  = ramin;
	gpuobj->vinst = ramin->start;
	return 0;
}

void
nv04_instmem_put(struct nouveau_gpuobj *gpuobj)
{
	struct nouveau_device *ndev = gpuobj->device;

	spin_lock(&ndev->ramin_lock);
	drm_mm_put_block(gpuobj->node);
	gpuobj->node = NULL;
	spin_unlock(&ndev->ramin_lock);
}

int
nv04_instmem_map(struct nouveau_gpuobj *gpuobj)
{
	gpuobj->pinst = gpuobj->vinst;
	return 0;
}

void
nv04_instmem_unmap(struct nouveau_gpuobj *gpuobj)
{
}

void
nv04_instmem_flush(struct nouveau_device *ndev)
{
}
