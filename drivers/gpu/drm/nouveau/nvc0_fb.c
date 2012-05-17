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
#include "nouveau_fb.h"

struct nvc0_fb_priv {
	struct nouveau_fb base;
	struct page *r100c10_page;
	dma_addr_t r100c10;
};

/* 0 = unsupported
 * 1 = non-compressed
 * 3 = compressed
 */
static const u8 types[256] = {
	1, 1, 3, 3, 3, 3, 0, 3, 3, 3, 3, 0, 0, 0, 0, 0,
	0, 1, 0, 0, 0, 0, 0, 3, 3, 3, 3, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 3, 3,
	3, 3, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 0, 1, 1, 1, 1, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 3, 3, 3, 3, 1, 1, 1, 1, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3,
	3, 3, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 3,
	3, 3, 0, 0, 0, 0, 0, 0, 3, 0, 0, 3, 0, 3, 0, 3,
	3, 0, 3, 3, 3, 3, 3, 0, 0, 3, 0, 3, 0, 3, 3, 0,
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 1, 1, 0
};

static bool
nvc0_fb_memtype_valid(struct nouveau_fb *pfb, u32 tile_flags)
{
	u8 memtype = (tile_flags & NOUVEAU_GEM_TILE_LAYOUT_MASK) >> 8;
	return likely((types[memtype] == 1));
}

static int
nvc0_fb_vram_new(struct nouveau_fb *pfb, u64 size, u32 align, u32 ncmin,
		 u32 memtype, struct nouveau_mem **pmem)
{
	struct nouveau_mm *mm = &pfb->ram.mm;
	struct nouveau_mm_node *r;
	struct nouveau_mem *mem;
	int type = (memtype & 0x0ff);
	int back = (memtype & 0x800);
	int ret;

	size  >>= 12;
	align >>= 12;
	ncmin >>= 12;
	if (!ncmin)
		ncmin = size;

	mem = kzalloc(sizeof(*mem), GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	INIT_LIST_HEAD(&mem->regions);
	mem->device = pfb->base.device;
	mem->memtype = type;
	mem->size = size;

	mutex_lock(&mm->mutex);
	do {
		if (back)
			ret = nouveau_mm_tail(mm, 1, size, ncmin, align, &r);
		else
			ret = nouveau_mm_head(mm, 1, size, ncmin, align, &r);
		if (ret) {
			mutex_unlock(&mm->mutex);
			pfb->ram.put(pfb, &mem);
			return ret;
		}

		list_add_tail(&r->rl_entry, &mem->regions);
		size -= r->length;
	} while (size);
	mutex_unlock(&mm->mutex);

	r = list_first_entry(&mem->regions, struct nouveau_mm_node, rl_entry);
	mem->offset = (u64)r->offset << 12;
	*pmem = mem;
	return 0;
}

static int
nvc0_fb_init(struct nouveau_device *ndev, int subdev)
{
	struct nvc0_fb_priv *priv = nv_subdev(ndev, subdev);
	nv_wr32(ndev, 0x100c10, priv->r100c10 >> 8);
	return 0;
}

static inline void
nvc0_mfb_subp_isr(struct nouveau_device *ndev, int unit, int subp)
{
	u32 subp_base = 0x141000 + (unit * 0x2000) + (subp * 0x400);
	u32 stat = nv_rd32(ndev, subp_base + 0x020);

	if (stat) {
		NV_INFO(ndev, "PMFB%d_SUBP%d: 0x%08x\n", unit, subp, stat);
		nv_wr32(ndev, subp_base + 0x020, stat);
	}
}

static void
nvc0_mfb_isr(struct nouveau_device *ndev)
{
	u32 units = nv_rd32(ndev, 0x00017c);
	while (units) {
		u32 subp, unit = ffs(units) - 1;
		for (subp = 0; subp < 2; subp++)
			nvc0_mfb_subp_isr(ndev, unit, subp);
		units &= ~(1 << unit);
	}

	/* we do something horribly wrong and upset PMFB a lot, so mask off
	 * interrupts from it after the first one until it's fixed
	 */
	nv_mask(ndev, 0x000640, 0x02000000, 0x00000000);
}

static void
nvc0_fb_destroy(struct nouveau_device *ndev, int subdev)
{
	struct nvc0_fb_priv *priv = nv_subdev(ndev, subdev);

	nouveau_irq_unregister(ndev, 25);

	if (priv->r100c10_page) {
		pci_unmap_page(ndev->dev->pdev, priv->r100c10, PAGE_SIZE,
			       PCI_DMA_BIDIRECTIONAL);
		__free_page(priv->r100c10_page);
	}

	nouveau_mm_fini(&priv->base.ram.mm);
}

static int
nvc0_vram_detect(struct nvc0_fb_priv *priv)
{
	struct nouveau_device *ndev = priv->base.base.device;
	struct nouveau_fb *pfb = &priv->base;
	const u32 rsvd_head = ( 256 * 1024) >> 12; /* vga memory */
	const u32 rsvd_tail = (1024 * 1024) >> 12; /* vbios etc */
	u32 parts = nv_rd32(ndev, 0x022438);
	u32 pmask = nv_rd32(ndev, 0x022554);
	u32 bsize = nv_rd32(ndev, 0x10f20c);
	u32 offset, length;
	bool uniform = true;
	int ret, part;

	NV_DEBUG(ndev, "0x100800: 0x%08x\n", nv_rd32(ndev, 0x100800));
	NV_DEBUG(ndev, "parts 0x%08x mask 0x%08x\n", parts, pmask);

	priv->base.ram.type = nouveau_mem_vbios_type(ndev);
	priv->base.ram.ranks = (nv_rd32(ndev, 0x10f200) & 0x00000004) ? 2 : 1;

	/* read amount of vram attached to each memory controller */
	for (part = 0; part < parts; part++) {
		if (!(pmask & (1 << part))) {
			u32 psize = nv_rd32(ndev, 0x11020c + (part * 0x1000));
			if (psize != bsize) {
				if (psize < bsize)
					bsize = psize;
				uniform = false;
			}

			NV_DEBUG(ndev, "%d: mem_amount 0x%08x\n", part, psize);
			priv->base.ram.size += (u64)psize << 20;
		}
	}

	/* if all controllers have the same amount attached, there's no holes */
	if (uniform) {
		offset = rsvd_head;
		length = (priv->base.ram.size >> 12) - rsvd_head - rsvd_tail;
		return nouveau_mm_init(&pfb->ram.mm, offset, length, 1);
	}

	/* otherwise, address lowest common amount from 0GiB */
	ret = nouveau_mm_init(&pfb->ram.mm, rsvd_head, (bsize << 8) * parts, 1);
	if (ret)
		return ret;

	/* and the rest starting from (8GiB + common_size) */
	offset = (0x0200000000ULL >> 12) + (bsize << 8);
	length = (priv->base.ram.size >> 12) - (bsize << 8) - rsvd_tail;

	ret = nouveau_mm_init(&pfb->ram.mm, offset, length, 0);
	if (ret) {
		nouveau_mm_fini(&pfb->ram.mm);
		return ret;
	}

	return 0;
}

int
nvc0_fb_create(struct nouveau_device *ndev, int subdev)
{
	struct nvc0_fb_priv *priv;
	int ret;

	ret = nouveau_subdev_create(ndev, subdev, "PFB", "fb", &priv);
	if (ret)
		return ret;

	priv->base.base.destroy = nvc0_fb_destroy;
	priv->base.base.init = nvc0_fb_init;
	priv->base.memtype_valid = nvc0_fb_memtype_valid;
	priv->base.ram.get = nvc0_fb_vram_new;
	priv->base.ram.put = nv50_fb_vram_del;

	ret = nvc0_vram_detect(priv);
	if (ret)
		goto done;

	priv->r100c10_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!priv->r100c10_page) {
		ret = -ENOMEM;
		goto done;
	}

	priv->r100c10 = pci_map_page(ndev->dev->pdev, priv->r100c10_page, 0,
				     PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
	if (pci_dma_mapping_error(ndev->dev->pdev, priv->r100c10)) {
		ret = -EFAULT;
		goto done;
	}

	nouveau_irq_register(ndev, 25, nvc0_mfb_isr);

done:
	return nouveau_subdev_init(ndev, subdev, ret);
}
