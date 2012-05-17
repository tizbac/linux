/*
 * Copyright (C) 2012 Red Hat Inc.
 *
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
#include "nouveau_vm.h"
#include "nouveau_instmem.h"
#include "nouveau_gpuobj.h"

struct nv50_instmem_priv {
	struct nouveau_instmem base;
	struct nouveau_mem *mem;
	struct nouveau_mm heap;
	u32 *suspend;
};

struct nv50_instmem_node {
	struct nouveau_mem *mem;
	struct nouveau_mm_node *bar;
	struct nouveau_vma chan_vma;
};

int
nv50_instmem_get(struct nouveau_instmem *pimem, struct nouveau_gpuobj *gpuobj,
		 struct nouveau_vm *vm, u32 size, u32 align)
{
	struct nouveau_fb *pfb = nv_subdev(pimem->base.device, NVDEV_SUBDEV_FB);
	struct nv50_instmem_node *node = NULL;
	int ret;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	size  = (size + 4095) & ~4095;
	align = max(align, (u32)4096);

	ret = pfb->ram.get(pfb, size, align, 0, 0x800, &node->mem);
	if (ret) {
		kfree(node);
		return ret;
	}

	node->mem->page_shift = 12;

	if (gpuobj->flags & NVOBJ_FLAG_VM) {
		u32 flags = NV_MEM_ACCESS_RW;
		if (!(gpuobj->flags & NVOBJ_FLAG_VM_USER))
			flags |= NV_MEM_ACCESS_SYS;

		ret = nouveau_vm_get(vm, size, node->mem->page_shift,
				     flags, &node->chan_vma);
		if (ret) {
			pfb->ram.put(pfb, &node->mem);
			kfree(node);
			return ret;
		}

		nouveau_vm_map(&node->chan_vma, node->mem);
		gpuobj->linst = node->chan_vma.offset;
	}

	gpuobj->vinst = node->mem->offset;
	gpuobj->size = size;
	gpuobj->node = node;
	return 0;
}

void
nv50_instmem_put(struct nouveau_instmem *pimem, struct nouveau_gpuobj *gpuobj)
{
	struct nouveau_fb *pfb = nv_subdev(pimem->base.device, NVDEV_SUBDEV_FB);
	struct nv50_instmem_node *node = gpuobj->node;

	if (node->chan_vma.node) {
		nouveau_vm_unmap(&node->chan_vma);
		nouveau_vm_put(&node->chan_vma);
	}

	pfb->ram.put(pfb, &node->mem);

	gpuobj->node = NULL;
	kfree(node);
}

static int
nv50_instmem_map(struct nouveau_instmem *pimem, struct nouveau_gpuobj *gpuobj)
{
	struct nouveau_device *ndev = pimem->base.device;
	struct nouveau_fb *pfb = nv_subdev(ndev, NVDEV_SUBDEV_FB);
	struct nv50_instmem_priv *priv = (void *)pimem;
	struct nv50_instmem_node *node = gpuobj->node;
	u64 offset = node->mem->offset;
	int ret;

	offset |= 0x00000001;
	if (pfb->ram.stolen) {
		offset += pfb->ram.stolen;
		offset |= 0x00000030;
	}

	mutex_lock(&priv->heap.mutex);
	ret = nouveau_mm_head(&priv->heap, 1, node->mem->size, node->mem->size,
			      1, &node->bar);
	mutex_unlock(&priv->heap.mutex);
	if (ret == 0) {
		u32 pgtptr = node->bar->offset * 8;
		u32 length = node->bar->length;
		while (length--) {
			nv_wi32(ndev, pgtptr + 0, lower_32_bits(offset));
			nv_wi32(ndev, pgtptr + 4, upper_32_bits(offset));
			offset += 0x1000;
			pgtptr += 8;
		}
		pimem->flush(pimem);

		gpuobj->pinst = (u64)node->bar->offset << 12;
	}

	nv50_vm_flush_engine(pimem->base.device, 6);
	return ret;
}

static void
nv50_instmem_unmap(struct nouveau_instmem *pimem, struct nouveau_gpuobj *gpuobj)
{
	struct nouveau_device *ndev = pimem->base.device;
	struct nv50_instmem_priv *priv = (void *)pimem;
	struct nv50_instmem_node *node = gpuobj->node;

	if (node->bar) {
		u32 pgtptr = node->bar->offset * 8;
		u32 length = node->bar->length;
		while (length--) {
			nv_wi32(ndev, pgtptr + 0, 0x00000000);
			nv_wi32(ndev, pgtptr + 4, 0x00000000);
			pgtptr += 8;
		}
		pimem->flush(pimem);

		mutex_lock(&priv->heap.mutex);
		nouveau_mm_free(&priv->heap, &node->bar);
		mutex_unlock(&priv->heap.mutex);
	}

	nv50_vm_flush_engine(pimem->base.device, 6);
}

static void
nv50_instmem_flush(struct nouveau_instmem *pimem)
{
	struct nouveau_device *ndev = pimem->base.device;
	unsigned long flags;

	spin_lock_irqsave(&ndev->vm_lock, flags);
	nv_wr32(ndev, 0x00330c, 0x00000001);
	if (!nv_wait(ndev, 0x00330c, 0x00000002, 0x00000000))
		NV_ERROR(ndev, "PRAMIN flush timeout\n");
	spin_unlock_irqrestore(&ndev->vm_lock, flags);
}

void
nv84_instmem_flush(struct nouveau_instmem *pimem)
{
	struct nouveau_device *ndev = pimem->base.device;
	unsigned long flags;

	spin_lock_irqsave(&ndev->vm_lock, flags);
	nv_wr32(ndev, 0x070000, 0x00000001);
	if (!nv_wait(ndev, 0x070000, 0x00000002, 0x00000000))
		NV_ERROR(ndev, "PRAMIN flush timeout\n");
	spin_unlock_irqrestore(&ndev->vm_lock, flags);
}

static int
nv50_instmem_init(struct nouveau_device *ndev, int subdev)
{
	struct nv50_instmem_priv *priv = nv_subdev(ndev, subdev);
	int i;

	nv_wr32(ndev, 0x001700, priv->mem->offset >> 16);

	if (priv->suspend) {
		for (i = 0; i < priv->mem->size << 12; i += 4)
			nv_wr32(ndev, 0x700000 + i, priv->suspend[i / 4]);
		vfree(priv->suspend);
		priv->suspend = NULL;
	}

	nv_mask(ndev, 0x000200, 0x00000100, 0x00000000);
	nv_mask(ndev, 0x000200, 0x00000100, 0x00000100);
	nv50_vm_flush_engine(ndev, 6);

	nv_wr32(ndev, 0x001704, 0x40000000 | priv->mem->offset >> 12);
	nv_wr32(ndev, 0x00170c, 0x80000442);
	for (i = 0; i < 8; i++)
		nv_wr32(ndev, 0x001900 + (i * 4), 0x00000000);

	for (i = 0; i < 64 * 1024; i += 4) {
		if (nv_rd32(ndev, 0x705000 + i) != nv_ri32(ndev, i)) {
			NV_ERROR(ndev, "INSTMEM: readback failed\n");
			return -EIO;
		}
	}

	return 0;
}

int
nv50_instmem_fini(struct nouveau_device *ndev, int subdev, bool suspend)
{
	struct nv50_instmem_priv *priv = nv_subdev(ndev, subdev);
	int i;

	if (suspend) {
		priv->suspend = vmalloc(priv->mem->size << 12);
		if (!priv->suspend)
			return -ENOMEM;

		nv_wr32(ndev, 0x001700, priv->mem->offset >> 16);
		for (i = 0; i < priv->mem->size << 12; i += 4)
			priv->suspend[i / 4] = nv_rd32(ndev, 0x700000 + i);
	}

	return 0;
}

void
nv50_instmem_destroy(struct nouveau_device *ndev, int subdev)
{
	struct nouveau_fb *pfb = nv_subdev(ndev, NVDEV_SUBDEV_FB);
	struct nv50_instmem_priv *priv = nv_subdev(ndev, subdev);
	nouveau_mm_fini(&priv->heap);
	pfb->ram.put(pfb, &priv->mem);
}

int
nv50_instmem_create(struct nouveau_device *ndev, int subdev)
{
	struct nouveau_fb *pfb = nv_subdev(ndev, NVDEV_SUBDEV_FB);
	struct nv50_instmem_priv *priv;
	u64 pgtlen, pgtend, pgtptr;
	u64 barlen, barend;
	u64 offset;
	int ret;

	ret = nouveau_subdev_create(ndev, subdev, "INSTMEM", "instmem", &priv);
	if (ret)
		return ret;

	priv->base.base.destroy = nv50_instmem_destroy;
	priv->base.base.init = nv50_instmem_init;
	priv->base.base.fini = nv50_instmem_fini;
	priv->base.get = nv50_instmem_get;
	priv->base.put = nv50_instmem_put;
	priv->base.map = nv50_instmem_map;
	priv->base.unmap = nv50_instmem_unmap;
	if (ndev->chipset == 0x50)
		priv->base.flush = nv50_instmem_flush;
	else
		priv->base.flush = nv84_instmem_flush;

	/* allocate memory for channel and ramin page tables */
	ret = pfb->ram.get(pfb, (20 + 64) * 1024, 65536, 0, 0x800, &priv->mem);
	if (ret)
		goto done;

	nv_wr32(ndev, 0x001700, priv->mem->offset >> 16);

	/* determine layout of channel address space */
	pgtptr = 0x5000 + 0x700000;
	offset = 0x5000 + priv->mem->offset;
	pgtlen = 32 * 1024 * 1024;
	barlen = min(pgtlen, (u64)pci_resource_len(ndev->dev->pdev, 3));
	pgtend = offset + pgtlen;
	barend = offset + ((barlen >> 12) * 8);

	/* BAR3 (RAMIN) hardcoded at +4GiB in channel address space */
	if (ndev->chipset == 0x50) {
		nv_wr32(ndev, 0x701440, lower_32_bits(offset) | 0x00000063);
		nv_wr32(ndev, 0x701444, upper_32_bits(offset));
	} else {
		nv_wr32(ndev, 0x700240, lower_32_bits(offset) | 0x00000063);
		nv_wr32(ndev, 0x700244, upper_32_bits(offset));
	}

	nv_wr32(ndev, 0x704420, 0x7fc00000);
	nv_wr32(ndev, 0x704424, pci_resource_len(ndev->dev->pdev, 3) - 1);
	nv_wr32(ndev, 0x704428, 0x00000000);
	nv_wr32(ndev, 0x70442c, 0x01000001);
	nv_wr32(ndev, 0x704430, 0x00000000);
	nv_wr32(ndev, 0x704434, 0x00000000);

	/* page table, init and map self into start of ramin */
	offset |= 0x00000001;
	if (pfb->ram.stolen) {
		offset += pfb->ram.stolen;
		offset |= 0x00000030;
	}

	for (; offset < barend; offset += 0x1000, pgtptr += 8) {
		nv_wr32(ndev, pgtptr + 0, lower_32_bits(offset));
		nv_wr32(ndev, pgtptr + 4, upper_32_bits(offset));
	}

	for (; offset < pgtend; offset += 0x1000, pgtptr += 8) {
		nv_wr32(ndev, pgtptr + 0, 0x00000000);
		nv_wr32(ndev, pgtptr + 4, 0x00000000);
	}

	offset = ((pgtlen >> 12) * 8) >> 12;
	barlen =  (barlen >> 12) - offset;

	ret = nouveau_mm_init(&priv->heap, offset, barlen, 1);
done:
	return nouveau_subdev_init(ndev, subdev, ret);
}
