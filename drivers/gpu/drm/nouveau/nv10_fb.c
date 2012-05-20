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

struct nv10_fb_priv {
	struct nouveau_fb base;
};

static void
nv10_fb_init_tile_region(struct nouveau_fb *pfb, int i, u32 addr,
			 u32 size, u32 pitch, u32 flags)
{
	struct nouveau_device *ndev = pfb->base.device;
	struct nouveau_tile_reg *tile = &ndev->tile.reg[i];

	tile->addr  = 0x80000000 | addr;
	tile->limit = max(1u, addr + size) - 1;
	tile->pitch = pitch;
}

static void
nv10_fb_free_tile_region(struct nouveau_fb *pfb, int i)
{
	struct nouveau_device *ndev = pfb->base.device;
	struct nouveau_tile_reg *tile = &ndev->tile.reg[i];

	tile->addr = tile->limit = tile->pitch = tile->zcomp = 0;
}

void
nv10_fb_set_tile_region(struct nouveau_fb *pfb, int i)
{
	struct nouveau_device *ndev = pfb->base.device;
	struct nouveau_tile_reg *tile = &ndev->tile.reg[i];

	nv_wr32(ndev, NV10_PFB_TLIMIT(i), tile->limit);
	nv_wr32(ndev, NV10_PFB_TSIZE(i), tile->pitch);
	nv_wr32(ndev, NV10_PFB_TILE(i), tile->addr);
}

int
nv10_fb_init(struct nouveau_device *ndev, int subdev)
{
	struct nouveau_fb *pfb = nv_subdev(ndev, subdev);
	int i;

	for (i = 0; i < pfb->num_tiles; i++)
		pfb->set_tile_region(pfb, i);

	return 0;
}

void
nv10_fb_destroy(struct nouveau_device *ndev, int subdev)
{
	struct nouveau_fb *pfb = nv_subdev(ndev, subdev);
	int i;

	for (i = 0; i < pfb->num_tiles; i++)
		pfb->free_tile_region(pfb, i);

	if (drm_mm_initialized(&pfb->tag_heap))
		drm_mm_takedown(&pfb->tag_heap);
}

int
nv10_fb_create(struct nouveau_device *ndev, int subdev)
{
	struct nv10_fb_priv *priv;
	int ret;

	ret = nouveau_subdev_create(ndev, subdev, "PFB", "fb", &priv);
	if (ret)
		return ret;

	if (ndev->chipset == 0x1a ||  ndev->chipset == 0x1f) {
		struct pci_dev *bridge;
		u32 mem, mib;

		bridge = pci_get_bus_and_slot(0, PCI_DEVFN(0, 1));
		if (!bridge) {
			NV_ERROR(ndev, "no bridge device\n");
			return 0;
		}

		if (ndev->chipset == 0x1a) {
			pci_read_config_dword(bridge, 0x7c, &mem);
			mib = ((mem >> 6) & 31) + 1;
		} else {
			pci_read_config_dword(bridge, 0x84, &mem);
			mib = ((mem >> 4) & 127) + 1;
		}

		ndev->vram_type = NV_MEM_TYPE_STOLEN;
		ndev->vram_size = mib * 1024 * 1024;
	} else {
		u32 data = nv_rd32(ndev, NV04_PFB_FIFO_DATA);
		u32 cfg0 = nv_rd32(ndev, 0x100200);

		if (cfg0 & 0x00000001)
			ndev->vram_type = NV_MEM_TYPE_DDR1;
		else
			ndev->vram_type = NV_MEM_TYPE_SDRAM;

		ndev->vram_size = data & NV10_PFB_FIFO_DATA_RAM_AMOUNT_MB_MASK;
	}

	priv->base.base.destroy = nv10_fb_destroy;
	priv->base.base.init = nv10_fb_init;
	priv->base.memtype_valid = nv04_fb_memtype_valid;
	priv->base.num_tiles = NV10_PFB_TILE__SIZE;
	priv->base.init_tile_region = nv10_fb_init_tile_region;
	priv->base.set_tile_region = nv10_fb_set_tile_region;
	priv->base.free_tile_region = nv10_fb_free_tile_region;
	return nouveau_subdev_init(ndev, subdev, ret);
}
