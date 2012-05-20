/*
 * Copyright (C) 2010 Francisco Jerez.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "drmP.h"

#include "nouveau_drv.h"
#include "nouveau_fb.h"
#include "nouveau_gpuobj.h"

struct nv40_fb_priv {
	struct nouveau_fb base;
};

static void
nv40_fb_set_tile_region(struct nouveau_fb *pfb, int i)
{
	struct nouveau_device *ndev = pfb->base.device;
	struct nouveau_tile_reg *tile = &ndev->tile.reg[i];

	switch (ndev->chipset) {
	case 0x40:
		nv_wr32(ndev, NV10_PFB_TLIMIT(i), tile->limit);
		nv_wr32(ndev, NV10_PFB_TSIZE(i), tile->pitch);
		nv_wr32(ndev, NV10_PFB_TILE(i), tile->addr);
		break;

	default:
		nv_wr32(ndev, NV40_PFB_TLIMIT(i), tile->limit);
		nv_wr32(ndev, NV40_PFB_TSIZE(i), tile->pitch);
		nv_wr32(ndev, NV40_PFB_TILE(i), tile->addr);
		break;
	}
}

static void
nv40_fb_init_gart(struct nouveau_device *ndev)
{
	struct nouveau_gpuobj *gart = ndev->gart_info.sg_ctxdma;

	if (ndev->gart_info.type != NOUVEAU_GART_HW) {
		nv_wr32(ndev, 0x100800, 0x00000001);
		return;
	}

	nv_wr32(ndev, 0x100800, gart->pinst | 0x00000002);
	nv_mask(ndev, 0x10008c, 0x00000100, 0x00000100);
	nv_wr32(ndev, 0x100820, 0x00000000);
}

static void
nv44_fb_init_gart(struct nouveau_device *ndev)
{
	struct nouveau_gpuobj *gart = ndev->gart_info.sg_ctxdma;
	u32 vinst;

	if (ndev->gart_info.type != NOUVEAU_GART_HW) {
		nv_wr32(ndev, 0x100850, 0x80000000);
		nv_wr32(ndev, 0x100800, 0x00000001);
		return;
	}

	/* calculate vram address of this PRAMIN block, object
	 * must be allocated on 512KiB alignment, and not exceed
	 * a total size of 512KiB for this to work correctly
	 */
	vinst  = nv_rd32(ndev, 0x10020c);
	vinst -= ((gart->pinst >> 19) + 1) << 19;

	nv_wr32(ndev, 0x100850, 0x80000000);
	nv_wr32(ndev, 0x100818, ndev->gart_info.dummy.addr);

	nv_wr32(ndev, 0x100804, ndev->gart_info.aper_size);
	nv_wr32(ndev, 0x100850, 0x00008000);
	nv_mask(ndev, 0x10008c, 0x00000200, 0x00000200);
	nv_wr32(ndev, 0x100820, 0x00000000);
	nv_wr32(ndev, 0x10082c, 0x00000001);
	nv_wr32(ndev, 0x100800, vinst | 0x00000010);
}

static int
nv40_fb_init(struct nouveau_device *ndev, int subdev)
{
	struct nv40_fb_priv *priv = nv_subdev(ndev, subdev);
	u32 tmp;
	int i;

	switch (ndev->chipset) {
	case 0x40:
	case 0x45:
		tmp = nv_rd32(ndev, NV10_PFB_CLOSE_PAGE2);
		nv_wr32(ndev, NV10_PFB_CLOSE_PAGE2, tmp & ~(1 << 15));
		break;
	default:
		if (nv44_graph_class(ndev))
			nv44_fb_init_gart(ndev);
		else
			nv40_fb_init_gart(ndev);
		break;
	}

	for (i = 0; i < priv->base.num_tiles; i++)
		priv->base.set_tile_region(&priv->base, i);

	return 0;
}

int
nv40_fb_create(struct nouveau_device *ndev, int subdev)
{
	struct nv40_fb_priv *priv;
	int ret;

	ret = nouveau_subdev_create(ndev, subdev, "PFB", "fb", &priv);
	if (ret)
		return ret;

	/* 0x001218 is actually present on a few other NV4X I looked at,
	 * and even contains sane values matching 0x100474.  From looking
	 * at various vbios images however, this isn't the case everywhere.
	 * So, I chose to use the same regs I've seen NVIDIA reading around
	 * the memory detection, hopefully that'll get us the right numbers
	 */
	if (ndev->chipset == 0x40) {
		u32 pbus1218 = nv_rd32(ndev, 0x001218);
		switch (pbus1218 & 0x00000300) {
		case 0x00000000: ndev->vram_type = NV_MEM_TYPE_SDRAM; break;
		case 0x00000100: ndev->vram_type = NV_MEM_TYPE_DDR1; break;
		case 0x00000200: ndev->vram_type = NV_MEM_TYPE_GDDR3; break;
		case 0x00000300: ndev->vram_type = NV_MEM_TYPE_DDR2; break;
		}
	} else
	if (ndev->chipset == 0x49 || ndev->chipset == 0x4b) {
		u32 pfb914 = nv_rd32(ndev, 0x100914);
		switch (pfb914 & 0x00000003) {
		case 0x00000000: ndev->vram_type = NV_MEM_TYPE_DDR1; break;
		case 0x00000001: ndev->vram_type = NV_MEM_TYPE_DDR2; break;
		case 0x00000002: ndev->vram_type = NV_MEM_TYPE_GDDR3; break;
		case 0x00000003: break;
		}
	} else
	if (ndev->chipset != 0x4e) {
		u32 pfb474 = nv_rd32(ndev, 0x100474);
		if (pfb474 & 0x00000004)
			ndev->vram_type = NV_MEM_TYPE_GDDR3;
		if (pfb474 & 0x00000002)
			ndev->vram_type = NV_MEM_TYPE_DDR2;
		if (pfb474 & 0x00000001)
			ndev->vram_type = NV_MEM_TYPE_DDR1;
	} else {
		ndev->vram_type = NV_MEM_TYPE_STOLEN;
	}

	ndev->vram_size = nv_rd32(ndev, 0x10020c) & 0xff000000;

	priv->base.base.destroy = nv10_fb_destroy;
	priv->base.base.init = nv40_fb_init;
	priv->base.memtype_valid = nv04_fb_memtype_valid;

	switch (ndev->chipset) {
	case 0x40:
	case 0x45:
		priv->base.num_tiles = NV10_PFB_TILE__SIZE;
		break;
	case 0x46:
	case 0x47:
	case 0x49:
	case 0x4b:
	case 0x4c:
		priv->base.num_tiles = NV40_PFB_TILE__SIZE_1;
		break;
	default:
		priv->base.num_tiles = NV40_PFB_TILE__SIZE_0;
		break;
	}
	priv->base.init_tile_region = nv30_fb_init_tile_region;
	priv->base.set_tile_region = nv40_fb_set_tile_region;
	priv->base.free_tile_region = nv30_fb_free_tile_region;

	return nouveau_subdev_init(ndev, subdev, ret);
}
