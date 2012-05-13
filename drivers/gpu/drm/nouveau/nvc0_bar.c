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
#include "nouveau_bar.h"
#include "nouveau_vm.h"
#include "nouveau_gpuobj.h"

struct nvc0_bar_priv {
	struct nouveau_bar base;
	struct nouveau_gpuobj *mem;
	struct nouveau_gpuobj *pgd;
	struct nouveau_vm *vm;
};

static int
nvc0_bar_map(struct nouveau_bar *pbar, struct nouveau_mem *mem)
{
	struct nvc0_bar_priv *priv = (void *)pbar;
	int ret;

	ret = nouveau_vm_get(priv->vm, mem->size << 12, mem->page_shift,
			     NV_MEM_ACCESS_RW, &mem->bar_vma);
	if (ret)
		return ret;

	nouveau_vm_map(&mem->bar_vma, mem);
	nvc0_vm_flush_engine(pbar->base.device, priv->pgd->vinst, 5);
	return 0;
}

static void
nvc0_bar_unmap(struct nouveau_bar *pbar, struct nouveau_mem *mem)
{
	struct nvc0_bar_priv *priv = (void *)pbar;

	if (mem->bar_vma.node) {
		nouveau_vm_unmap(&mem->bar_vma);
		nouveau_vm_put(&mem->bar_vma);
	}

	nvc0_vm_flush_engine(pbar->base.device, priv->pgd->vinst, 5);
}

static int
nvc0_bar_init(struct nouveau_device *ndev, int subdev)
{
	struct nvc0_bar_priv *priv = nv_subdev(ndev, NVDEV_SUBDEV_BAR);

	nv_wr32(ndev, 0x001704, 0x80000000 | priv->mem->vinst >> 12);
	return 0;
}

static void
nvc0_bar_destroy(struct nouveau_device *ndev, int subdev)
{
	struct nvc0_bar_priv *priv = nv_subdev(ndev, subdev);
	nouveau_vm_ref(NULL, &priv->vm, priv->pgd);
	nouveau_gpuobj_ref(NULL, &priv->pgd);
	nouveau_gpuobj_ref(NULL, &priv->mem);
}

int
nvc0_bar_create(struct nouveau_device *ndev, int subdev)
{
	struct nvc0_bar_priv *priv;
	struct nouveau_vm *vm;
	u64 size = pci_resource_len(ndev->dev->pdev, 1);
	int ret;

	ret = nouveau_subdev_create(ndev, subdev, "BAR", "bar1", &priv);
	if (ret)
		return ret;

	priv->base.base.destroy = nvc0_bar_destroy;
	priv->base.base.init = nvc0_bar_init;
	priv->base.map = nvc0_bar_map;
	priv->base.unmap = nvc0_bar_unmap;

	ret = nouveau_gpuobj_new(ndev, NULL, 0x1000, 0x1000, 0, &priv->mem);
	if (ret)
		goto done;

	ret = nouveau_gpuobj_new(ndev, NULL, 0x8000, 0x1000, 0, &priv->pgd);
	if (ret)
		goto done;

	nv_wo32(priv->mem, 0x0200, lower_32_bits(priv->pgd->vinst));
	nv_wo32(priv->mem, 0x0204, upper_32_bits(priv->pgd->vinst));
	nv_wo32(priv->mem, 0x0208, lower_32_bits(size - 1));
	nv_wo32(priv->mem, 0x020c, upper_32_bits(size - 1));

	ret = nouveau_vm_new(ndev, 0, size, 0, &vm);
	if (ret)
		goto done;

	ret = nouveau_vm_ref(vm, &priv->vm, priv->pgd);
	nouveau_vm_ref(NULL, &vm, NULL);
	if (ret)
		goto done;

	/* create vm for kernel mappings, doesn't really belong here.. */
	ret = nouveau_vm_new(ndev, 0, (1ULL << 40), 0x0020000000ULL,
			    &ndev->chan_vm);
	if (ret)
		goto done;

done:
	return nouveau_subdev_init(ndev, subdev, ret);
}
