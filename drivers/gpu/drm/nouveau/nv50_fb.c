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
#include "nouveau_fifo.h"

struct nv50_fb_priv {
	struct nouveau_fb base;
	struct page *r100c08_page;
	dma_addr_t r100c08;
};

static int types[0x80] = {
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 0, 0, 0, 0, 2, 2, 2, 2, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 0, 2, 2, 2, 2, 2, 2, 2, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 0, 0,
	0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 2, 2, 2, 2,
	1, 0, 2, 0, 1, 0, 2, 0, 1, 1, 2, 2, 1, 1, 0, 0
};

static bool
nv50_fb_memtype_valid(struct nouveau_fb *pfb, u32 tile_flags)
{
	int type = (tile_flags & NOUVEAU_GEM_TILE_LAYOUT_MASK) >> 8;

	if (likely(type < ARRAY_SIZE(types) && types[type]))
		return true;
	return false;
}

static int
nv50_fb_vram_new(struct nouveau_fb *pfb, u64 size, u32 align, u32 ncmin,
		 u32 memtype, struct nouveau_mem **pmem)
{
	struct nouveau_mm *mm = &pfb->mm;
	struct nouveau_mm_node *r;
	struct nouveau_mem *mem;
	int comp = (memtype & 0x300) >> 8;
	int type = (memtype & 0x07f);
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

	mutex_lock(&mm->mutex);
	if (comp) {
		if (align == 16) {
			int n = (size >> 4) * comp;

			mem->tag = drm_mm_search_free(&pfb->tag_heap, n, 0, 0);
			if (mem->tag)
				mem->tag = drm_mm_get_block(mem->tag, n, 0);
		}

		if (unlikely(!mem->tag))
			comp = 0;
	}

	INIT_LIST_HEAD(&mem->regions);
	mem->device = pfb->base.device;
	mem->memtype = (comp << 7) | type;
	mem->size = size;

	type = types[type];
	do {
		if (back)
			ret = nouveau_mm_tail(mm, type, size, ncmin, align, &r);
		else
			ret = nouveau_mm_head(mm, type, size, ncmin, align, &r);
		if (ret) {
			mutex_unlock(&mm->mutex);
			pfb->vram_put(pfb, &mem);
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

void
nv50_fb_vram_del(struct nouveau_fb *pfb, struct nouveau_mem **pmem)
{
	struct nouveau_mm *mm = &pfb->mm;
	struct nouveau_mm_node *this;
	struct nouveau_mem *mem;

	mem = *pmem;
	*pmem = NULL;
	if (unlikely(mem == NULL))
		return;

	mutex_lock(&mm->mutex);
	while (!list_empty(&mem->regions)) {
		this = list_first_entry(&mem->regions, struct nouveau_mm_node, rl_entry);

		list_del(&this->rl_entry);
		nouveau_mm_free(mm, &this);
	}

	if (mem->tag) {
		drm_mm_put_block(mem->tag);
		mem->tag = NULL;
	}
	mutex_unlock(&mm->mutex);

	kfree(mem);
}

static int
nv50_fb_init(struct nouveau_device *ndev, int subdev)
{
	struct nv50_fb_priv *priv = nv_subdev(ndev, subdev);

	/* Not a clue what this is exactly.  Without pointing it at a
	 * scratch page, VRAM->GART blits with M2MF (as in DDX DFS)
	 * cause IOMMU "read from address 0" errors (rh#561267)
	 */
	nv_wr32(ndev, 0x100c08, priv->r100c08 >> 8);

	/* This is needed to get meaningful information from 100c90
	 * on traps. No idea what these values mean exactly. */
	switch (ndev->chipset) {
	case 0x50:
		nv_wr32(ndev, 0x100c90, 0x000707ff);
		break;
	case 0xa3:
	case 0xa5:
	case 0xa8:
		nv_wr32(ndev, 0x100c90, 0x000d0fff);
		break;
	case 0xaf:
		nv_wr32(ndev, 0x100c90, 0x089d1fff);
		break;
	default:
		nv_wr32(ndev, 0x100c90, 0x001d07ff);
		break;
	}

	return 0;
}

static void
nv50_fb_destroy(struct nouveau_device *ndev, int subdev)
{
	struct nv50_fb_priv *priv = nv_subdev(ndev, subdev);

	if (drm_mm_initialized(&priv->base.tag_heap))
		drm_mm_takedown(&priv->base.tag_heap);

	if (priv->r100c08_page) {
		pci_unmap_page(ndev->dev->pdev, priv->r100c08, PAGE_SIZE,
			       PCI_DMA_BIDIRECTIONAL);
		__free_page(priv->r100c08_page);
	}

	nouveau_mm_fini(&priv->base.mm);
}

static u32
nv50_vram_rblock(struct nouveau_device *ndev)
{
	int i, parts, colbits, rowbitsa, rowbitsb, banks;
	u64 rowsize, predicted;
	u32 r0, r4, rt, ru, rblock_size;

	r0 = nv_rd32(ndev, 0x100200);
	r4 = nv_rd32(ndev, 0x100204);
	rt = nv_rd32(ndev, 0x100250);
	ru = nv_rd32(ndev, 0x001540);
	NV_DEBUG(ndev, "memcfg 0x%08x 0x%08x 0x%08x 0x%08x\n", r0, r4, rt, ru);

	for (i = 0, parts = 0; i < 8; i++) {
		if (ru & (0x00010000 << i))
			parts++;
	}

	colbits  =  (r4 & 0x0000f000) >> 12;
	rowbitsa = ((r4 & 0x000f0000) >> 16) + 8;
	rowbitsb = ((r4 & 0x00f00000) >> 20) + 8;
	banks    = 1 << (((r4 & 0x03000000) >> 24) + 2);

	rowsize = parts * banks * (1 << colbits) * 8;
	predicted = rowsize << rowbitsa;
	if (r0 & 0x00000004)
		predicted += rowsize << rowbitsb;

	if (predicted != ndev->vram_size) {
		NV_WARN(ndev, "memory controller reports %dMiB VRAM\n",
			(u32)(ndev->vram_size >> 20));
		NV_WARN(ndev, "we calculated %dMiB VRAM\n",
			(u32)(predicted >> 20));
	}

	rblock_size = rowsize;
	if (rt & 1)
		rblock_size *= 3;

	NV_DEBUG(ndev, "rblock %d bytes\n", rblock_size);
	return rblock_size;
}

static int
nv50_vram_detect(struct nv50_fb_priv *priv)
{
	struct nouveau_device *ndev = priv->base.base.device;
	const u32 rsvd_head = ( 256 * 1024) >> 12; /* vga memory */
	const u32 rsvd_tail = (1024 * 1024) >> 12; /* vbios etc */
	u32 pfb714 = nv_rd32(ndev, 0x100714);
	u32 rblock, length;

	switch (pfb714 & 0x00000007) {
	case 0: ndev->vram_type = NV_MEM_TYPE_DDR1; break;
	case 1:
		if (nouveau_mem_vbios_type(ndev) == NV_MEM_TYPE_DDR3)
			ndev->vram_type = NV_MEM_TYPE_DDR3;
		else
			ndev->vram_type = NV_MEM_TYPE_DDR2;
		break;
	case 2: ndev->vram_type = NV_MEM_TYPE_GDDR3; break;
	case 3: ndev->vram_type = NV_MEM_TYPE_GDDR4; break;
	case 4: ndev->vram_type = NV_MEM_TYPE_GDDR5; break;
	default:
		break;
	}

	ndev->vram_rank_B = !!(nv_rd32(ndev, 0x100200) & 0x4);
	ndev->vram_size  = nv_rd32(ndev, 0x10020c);
	ndev->vram_size |= (ndev->vram_size & 0xff) << 32;
	ndev->vram_size &= 0xffffffff00ULL;

	/* IGPs, no funky reordering happens here, they don't have VRAM */
	if (ndev->chipset == 0xaa ||
	    ndev->chipset == 0xac ||
	    ndev->chipset == 0xaf) {
		ndev->vram_sys_base = (u64)nv_rd32(ndev, 0x100e10) << 12;
		rblock = 4096 >> 12;
	} else {
		rblock = nv50_vram_rblock(ndev) >> 12;
	}

	length = (ndev->vram_size >> 12) - rsvd_head - rsvd_tail;

	return nouveau_mm_init(&priv->base.mm, rsvd_head, length, rblock);
}

int
nv50_fb_create(struct nouveau_device *ndev, int subdev)
{
	struct nv50_fb_priv *priv;
	u32 tagmem;
	int ret;

	ret = nouveau_subdev_create(ndev, subdev, "PFB", "fb", &priv);
	if (ret)
		return ret;

	priv->base.base.destroy = nv50_fb_destroy;
	priv->base.base.init = nv50_fb_init;
	priv->base.memtype_valid = nv50_fb_memtype_valid;
	priv->base.vram_get = nv50_fb_vram_new;
	priv->base.vram_put = nv50_fb_vram_del;

	ret = nv50_vram_detect(priv);
	if (ret)
		goto done;

	priv->r100c08_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!priv->r100c08_page) {
		ret = -ENOMEM;
		goto done;
	}

	priv->r100c08 = pci_map_page(ndev->dev->pdev, priv->r100c08_page, 0,
				     PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
	if (pci_dma_mapping_error(ndev->dev->pdev, priv->r100c08)) {
		ret = -EFAULT;
		goto done;
	}

	tagmem = nv_rd32(ndev, 0x100320);
	NV_DEBUG(ndev, "%d tags available\n", tagmem);

	ret = drm_mm_init(&priv->base.tag_heap, 0, tagmem);
done:
	return nouveau_subdev_init(ndev, subdev, ret);
}

static struct nouveau_enum vm_dispatch_subclients[] = {
	{ 0x00000000, "GRCTX", NULL },
	{ 0x00000001, "NOTIFY", NULL },
	{ 0x00000002, "QUERY", NULL },
	{ 0x00000003, "COND", NULL },
	{ 0x00000004, "M2M_IN", NULL },
	{ 0x00000005, "M2M_OUT", NULL },
	{ 0x00000006, "M2M_NOTIFY", NULL },
	{}
};

static struct nouveau_enum vm_ccache_subclients[] = {
	{ 0x00000000, "CB", NULL },
	{ 0x00000001, "TIC", NULL },
	{ 0x00000002, "TSC", NULL },
	{}
};

static struct nouveau_enum vm_prop_subclients[] = {
	{ 0x00000000, "RT0", NULL },
	{ 0x00000001, "RT1", NULL },
	{ 0x00000002, "RT2", NULL },
	{ 0x00000003, "RT3", NULL },
	{ 0x00000004, "RT4", NULL },
	{ 0x00000005, "RT5", NULL },
	{ 0x00000006, "RT6", NULL },
	{ 0x00000007, "RT7", NULL },
	{ 0x00000008, "ZETA", NULL },
	{ 0x00000009, "LOCAL", NULL },
	{ 0x0000000a, "GLOBAL", NULL },
	{ 0x0000000b, "STACK", NULL },
	{ 0x0000000c, "DST2D", NULL },
	{}
};

static struct nouveau_enum vm_pfifo_subclients[] = {
	{ 0x00000000, "PUSHBUF", NULL },
	{ 0x00000001, "SEMAPHORE", NULL },
	{}
};

static struct nouveau_enum vm_bar_subclients[] = {
	{ 0x00000000, "FB", NULL },
	{ 0x00000001, "IN", NULL },
	{}
};

static struct nouveau_enum vm_client[] = {
	{ 0x00000000, "STRMOUT", NULL },
	{ 0x00000003, "DISPATCH", vm_dispatch_subclients },
	{ 0x00000004, "PFIFO_WRITE", NULL },
	{ 0x00000005, "CCACHE", vm_ccache_subclients },
	{ 0x00000006, "PPPP", NULL },
	{ 0x00000007, "CLIPID", NULL },
	{ 0x00000008, "PFIFO_READ", NULL },
	{ 0x00000009, "VFETCH", NULL },
	{ 0x0000000a, "TEXTURE", NULL },
	{ 0x0000000b, "PROP", vm_prop_subclients },
	{ 0x0000000c, "PVP", NULL },
	{ 0x0000000d, "PBSP", NULL },
	{ 0x0000000e, "PCRYPT", NULL },
	{ 0x0000000f, "PCOUNTER", NULL },
	{ 0x00000011, "PDAEMON", NULL },
	{}
};

static struct nouveau_enum vm_engine[] = {
	{ 0x00000000, "PGRAPH", NULL },
	{ 0x00000001, "PVP", NULL },
	{ 0x00000004, "PEEPHOLE", NULL },
	{ 0x00000005, "PFIFO", vm_pfifo_subclients },
	{ 0x00000006, "BAR", vm_bar_subclients },
	{ 0x00000008, "PPPP", NULL },
	{ 0x00000009, "PBSP", NULL },
	{ 0x0000000a, "PCRYPT", NULL },
	{ 0x0000000b, "PCOUNTER", NULL },
	{ 0x0000000c, "SEMAPHORE_BG", NULL },
	{ 0x0000000d, "PCOPY", NULL },
	{ 0x0000000e, "PDAEMON", NULL },
	{}
};

static struct nouveau_enum vm_fault[] = {
	{ 0x00000000, "PT_NOT_PRESENT", NULL },
	{ 0x00000001, "PT_TOO_SHORT", NULL },
	{ 0x00000002, "PAGE_NOT_PRESENT", NULL },
	{ 0x00000003, "PAGE_SYSTEM_ONLY", NULL },
	{ 0x00000004, "PAGE_READ_ONLY", NULL },
	{ 0x00000006, "NULL_DMAOBJ", NULL },
	{ 0x00000007, "WRONG_MEMTYPE", NULL },
	{ 0x0000000b, "VRAM_LIMIT", NULL },
	{ 0x0000000f, "DMAOBJ_LIMIT", NULL },
	{}
};

void
nv50_fb_vm_trap(struct nouveau_device *ndev, int display)
{
	struct nouveau_fifo_priv *pfifo = nv_engine(ndev, NVOBJ_ENGINE_FIFO);
	const struct nouveau_enum *en, *cl;
	unsigned long flags;
	u32 trap[6], idx, chinst;
	u8 st0, st1, st2, st3;
	int i, ch;

	idx = nv_rd32(ndev, 0x100c90);
	if (!(idx & 0x80000000))
		return;
	idx &= 0x00ffffff;

	for (i = 0; i < 6; i++) {
		nv_wr32(ndev, 0x100c90, idx | i << 24);
		trap[i] = nv_rd32(ndev, 0x100c94);
	}
	nv_wr32(ndev, 0x100c90, idx | 0x80000000);

	if (!display)
		return;

	/* lookup channel id */
	chinst = (trap[2] << 16) | trap[1];
	spin_lock_irqsave(&ndev->channels.lock, flags);
	for (ch = 0; ch < pfifo->channels; ch++) {
		struct nouveau_channel *chan = ndev->channels.ptr[ch];

		if (!chan || !chan->ramin)
			continue;

		if (chinst == chan->ramin->vinst >> 12)
			break;
	}
	spin_unlock_irqrestore(&ndev->channels.lock, flags);

	/* decode status bits into something more useful */
	if (ndev->chipset  < 0xa3 ||
	    ndev->chipset == 0xaa || ndev->chipset == 0xac) {
		st0 = (trap[0] & 0x0000000f) >> 0;
		st1 = (trap[0] & 0x000000f0) >> 4;
		st2 = (trap[0] & 0x00000f00) >> 8;
		st3 = (trap[0] & 0x0000f000) >> 12;
	} else {
		st0 = (trap[0] & 0x000000ff) >> 0;
		st1 = (trap[0] & 0x0000ff00) >> 8;
		st2 = (trap[0] & 0x00ff0000) >> 16;
		st3 = (trap[0] & 0xff000000) >> 24;
	}

	NV_INFO(ndev, "VM: trapped %s at 0x%02x%04x%04x on ch %d [0x%08x] ",
		(trap[5] & 0x00000100) ? "read" : "write",
		trap[5] & 0xff, trap[4] & 0xffff, trap[3] & 0xffff, ch, chinst);

	en = nouveau_enum_find(vm_engine, st0);
	if (en)
		printk("%s/", en->name);
	else
		printk("%02x/", st0);

	cl = nouveau_enum_find(vm_client, st2);
	if (cl)
		printk("%s/", cl->name);
	else
		printk("%02x/", st2);

	if      (cl && cl->data) cl = nouveau_enum_find(cl->data, st3);
	else if (en && en->data) cl = nouveau_enum_find(en->data, st3);
	else                     cl = NULL;
	if (cl)
		printk("%s", cl->name);
	else
		printk("%02x", st3);

	printk(" reason: ");
	en = nouveau_enum_find(vm_fault, st1);
	if (en)
		printk("%s\n", en->name);
	else
		printk("0x%08x\n", st1);
}
