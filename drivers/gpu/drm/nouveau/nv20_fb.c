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

struct nv20_fb_priv {
	struct nouveau_fb base;
};

static struct drm_mm_node *
nv20_fb_alloc_tag(struct nouveau_fb *pfb, u32 size)
{
	struct nouveau_device *ndev = pfb->base.device;
	struct drm_mm_node *mem;
	int ret;

	ret = drm_mm_pre_get(&pfb->tag_heap);
	if (ret)
		return NULL;

	spin_lock(&ndev->tile.lock);
	mem = drm_mm_search_free(&pfb->tag_heap, size, 0, 0);
	if (mem)
		mem = drm_mm_get_block_atomic(mem, size, 0);
	spin_unlock(&ndev->tile.lock);

	return mem;
}

static void
nv20_fb_free_tag(struct nouveau_fb *pfb, struct drm_mm_node **pmem)
{
	struct nouveau_device *ndev = pfb->base.device;
	struct drm_mm_node *mem = *pmem;
	if (mem) {
		spin_lock(&ndev->tile.lock);
		drm_mm_put_block(mem);
		spin_unlock(&ndev->tile.lock);
		*pmem = NULL;
	}
}

static void
nv20_fb_init_tile_region(struct nouveau_fb *pfb, int i, u32 addr,
			 u32 size, u32 pitch, u32 flags)
{
	struct nouveau_device *ndev = pfb->base.device;
	struct nouveau_tile_reg *tile = &ndev->tile.reg[i];
	int bpp = (flags & NOUVEAU_GEM_TILE_32BPP ? 32 : 16);

	tile->addr  = 0x00000001 | addr;
	tile->limit = max(1u, addr + size) - 1;
	tile->pitch = pitch;

	/* Allocate some of the on-die tag memory, used to store Z
	 * compression meta-data (most likely just a bitmap determining
	 * if a given tile is compressed or not).
	 */
	if (flags & NOUVEAU_GEM_TILE_ZETA) {
		tile->tag_mem = nv20_fb_alloc_tag(pfb, size / 256);
		if (tile->tag_mem) {
			/* Enable Z compression */
			tile->zcomp = tile->tag_mem->start;
			if (ndev->chipset >= 0x25) {
				if (bpp == 16)
					tile->zcomp |= NV25_PFB_ZCOMP_MODE_16;
				else
					tile->zcomp |= NV25_PFB_ZCOMP_MODE_32;
			} else {
				tile->zcomp |= NV20_PFB_ZCOMP_EN;
				if (bpp != 16)
					tile->zcomp |= NV20_PFB_ZCOMP_MODE_32;
			}
		}

		tile->addr |= 2;
	}
}

static void
nv20_fb_free_tile_region(struct nouveau_fb *pfb, int i)
{
	struct nouveau_device *ndev = pfb->base.device;
	struct nouveau_tile_reg *tile = &ndev->tile.reg[i];

	tile->addr = tile->limit = tile->pitch = tile->zcomp = 0;
	nv20_fb_free_tag(pfb, &tile->tag_mem);
}

static void
nv20_fb_set_tile_region(struct nouveau_fb *pfb, int i)
{
	struct nouveau_device *ndev = pfb->base.device;
	struct nouveau_tile_reg *tile = &ndev->tile.reg[i];

	nv_wr32(ndev, NV10_PFB_TLIMIT(i), tile->limit);
	nv_wr32(ndev, NV10_PFB_TSIZE(i), tile->pitch);
	nv_wr32(ndev, NV10_PFB_TILE(i), tile->addr);
	nv_wr32(ndev, NV20_PFB_ZCOMP(i), tile->zcomp);
}

int
nv20_fb_create(struct nouveau_device *ndev, int subdev)
{
	u32 mem_size = nv_rd32(ndev, 0x10020c);
	u32 pbus1218 = nv_rd32(ndev, 0x001218);
	struct nv20_fb_priv *priv;
	int ret;

	ret = nouveau_subdev_create(ndev, subdev, "PFB", "fb", &priv);
	if (ret)
		return ret;

	switch (pbus1218 & 0x00000300) {
	case 0x00000000: priv->base.ram.type = NV_MEM_TYPE_SDRAM; break;
	case 0x00000100: priv->base.ram.type = NV_MEM_TYPE_DDR1; break;
	case 0x00000200: priv->base.ram.type = NV_MEM_TYPE_GDDR3; break;
	case 0x00000300: priv->base.ram.type = NV_MEM_TYPE_GDDR2; break;
	}
	priv->base.ram.size = mem_size & 0xff000000;

	if (ndev->chipset >= 0x25)
		drm_mm_init(&priv->base.tag_heap, 0, 64 * 1024);
	else
		drm_mm_init(&priv->base.tag_heap, 0, 32 * 1024);

	priv->base.base.destroy = nv10_fb_destroy;
	priv->base.base.init = nv10_fb_init;
	priv->base.memtype_valid = nv04_fb_memtype_valid;
	priv->base.num_tiles = NV10_PFB_TILE__SIZE;
	priv->base.init_tile_region = nv20_fb_init_tile_region;
	priv->base.set_tile_region = nv20_fb_set_tile_region;
	priv->base.free_tile_region = nv20_fb_free_tile_region;
	return nouveau_subdev_init(ndev, subdev, ret);
}
