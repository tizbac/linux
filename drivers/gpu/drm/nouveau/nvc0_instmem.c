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

struct nvc0_instmem_priv {
	struct nouveau_instmem base;
	struct nouveau_mem *mem;
	struct nouveau_mm heap;
	u32 *suspend;
	u64 pgd;
};

struct nvc0_instmem_node {
	struct nouveau_mem *mem;
	struct nouveau_mm_node *bar;
	struct nouveau_vma chan_vma;
};

static int
nvc0_instmem_map(struct nouveau_instmem *pimem, struct nouveau_gpuobj *gpuobj)
{
	struct nouveau_device *ndev = pimem->base.device;
	struct nvc0_instmem_priv *priv = (void *)pimem;
	struct nvc0_instmem_node *node = gpuobj->node;
	u64 offset = node->mem->offset;
	int ret;

	mutex_lock(&priv->heap.mutex);
	ret = nouveau_mm_head(&priv->heap, 1, node->mem->size, node->mem->size,
			      1, &node->bar);
	mutex_unlock(&priv->heap.mutex);
	if (ret == 0) {
		u32 pgtptr = node->bar->offset * 8;
		u32 length = node->bar->length;
		while (length--) {
			nv_wi32(ndev, pgtptr + 0, 0x00000001 | (offset >> 8));
			nv_wi32(ndev, pgtptr + 4, 0x00000000);
			offset += 0x1000;
			pgtptr += 8;
		}
		pimem->flush(pimem);

		gpuobj->pinst = node->bar->offset << 12;
	}

	nvc0_vm_flush_engine(pimem->base.device, priv->pgd, 5);
	return ret;
}

static void
nvc0_instmem_unmap(struct nouveau_instmem *pimem, struct nouveau_gpuobj *gpuobj)
{
	struct nouveau_device *ndev = pimem->base.device;
	struct nvc0_instmem_priv *priv = (void *)pimem;
	struct nvc0_instmem_node *node = gpuobj->node;

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

	nvc0_vm_flush_engine(pimem->base.device, priv->pgd, 5);
}

static int
nvc0_instmem_init(struct nouveau_device *ndev, int subdev)
{
	struct nvc0_instmem_priv *priv = nv_subdev(ndev, subdev);
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
	nv_mask(ndev, 0x100c80, 0x00000001, 0x00000000);
	nvc0_vm_flush_engine(ndev, priv->pgd, 5);

	nv_wr32(ndev, 0x001714, 0xc0000000 | priv->mem->offset >> 12);

	for (i = 0; i < 64 * 1024; i += 4) {
		if (nv_rd32(ndev, 0x702000 + i) != nv_ri32(ndev, i)) {
			NV_ERROR(ndev, "INSTMEM: readback failed\n");
			return -EIO;
		}
	}

	return 0;
}

int
nvc0_instmem_create(struct nouveau_device *ndev, int subdev)
{
	struct nouveau_fb *pfb = nv_subdev(ndev, NVDEV_SUBDEV_FB);
	struct nvc0_instmem_priv *priv;
	u64 pgtlen, pgtend, pgtptr;
	u64 barlen, barend;
	u64 offset;
	int ret;

	ret = nouveau_subdev_create(ndev, subdev, "INSTMEM", "instmem", &priv);
	if (ret)
		return ret;

	priv->base.base.destroy = nv50_instmem_destroy;
	priv->base.base.init = nvc0_instmem_init;
	priv->base.base.fini = nv50_instmem_fini;
	priv->base.get = nv50_instmem_get;
	priv->base.put = nv50_instmem_put;
	priv->base.map = nvc0_instmem_map;
	priv->base.unmap = nvc0_instmem_unmap;
	priv->base.flush = nv84_instmem_flush;

	/* allocate memory for memory and ramin page tables */
	ret = pfb->ram.get(pfb, (8 + 64) * 1024, 65536, 0, 0x800, &priv->mem);
	if (ret)
		goto done;

	nv_wr32(ndev, 0x001700, priv->mem->offset >> 16);

	/* determine layout of channel memory */
	offset = 0x2000 + priv->mem->offset;
	pgtptr = 0x2000 + 0x700000;
	pgtlen = 32 * 1024 * 1024;
	barlen = min(pgtlen, (u64)pci_resource_len(ndev->dev->pdev, 3));
	pgtend = offset + pgtlen;
	barend = offset + barlen;

	/* channel descriptor, point at page directory */
	nv_wr32(ndev, 0x700200, lower_32_bits(priv->mem->offset + 0x1000));
	nv_wr32(ndev, 0x700204, upper_32_bits(priv->mem->offset + 0x1000));
	nv_wr32(ndev, 0x700208, lower_32_bits(barlen - 1));
	nv_wr32(ndev, 0x70020c, upper_32_bits(barlen - 1));

	/* page directory, 4KiB-pages entry 0 points at ramin page table */
	nv_wr32(ndev, 0x701000, 0x00000008);
	nv_wr32(ndev, 0x701004, 0x00000001 | (offset >> 8));

	/* page table, init and map self into start of ramin */
	for (; offset < barend; offset += 0x1000, pgtptr += 8) {
		nv_wr32(ndev, pgtptr + 0, 0x00000001 | (offset >> 8));
		nv_wr32(ndev, pgtptr + 4, 0x00000000);
	}

	for (; offset < pgtend; offset += 0x1000, pgtptr += 8) {
		nv_wr32(ndev, pgtptr + 0, 0x00000000);
		nv_wr32(ndev, pgtptr + 4, 0x00000000);
	}

	priv->pgd = priv->mem->offset + 0x1000;

	offset = ((pgtlen >> 12) * 8) >> 12;
	barlen =  (barlen >> 12) - offset;

	ret = nouveau_mm_init(&priv->heap, offset, barlen, 1);
done:
	return nouveau_subdev_init(ndev, subdev, ret);
}
