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

struct nv50_bar_priv {
	struct nouveau_bar base;
	struct nouveau_gpuobj *pgd;
	struct nouveau_vm *vm;
};

static int
nv50_bar_map(struct nouveau_bar *pbar, struct nouveau_mem *mem)
{
	struct nv50_bar_priv *priv = (void *)pbar;
	int ret;

	ret = nouveau_vm_get(priv->vm, mem->size << 12, 12, NV_MEM_ACCESS_RW,
			     &mem->bar_vma);
	if (ret)
		return ret;

	nouveau_vm_map(&mem->bar_vma, mem);
	nv50_vm_flush_engine(pbar->base.device, 6);
	return 0;
}

static void
nv50_bar_unmap(struct nouveau_bar *pbar, struct nouveau_mem *mem)
{
	if (mem->bar_vma.node) {
		nouveau_vm_unmap(&mem->bar_vma);
		nouveau_vm_put(&mem->bar_vma);
	}

	nv50_vm_flush_engine(pbar->base.device, 6);
}

static int
nv50_bar_init(struct nouveau_device *ndev, int subdev)
{
	nv_wr32(ndev, 0x001708, 0x80000440);
	return 0;
}

static void
nv50_bar_destroy(struct nouveau_device *ndev, int subdev)
{
	struct nv50_bar_priv *priv = nv_subdev(ndev, subdev);
	nouveau_vm_ref(NULL, &priv->vm, priv->pgd);
	nouveau_gpuobj_ref(NULL, &priv->pgd);
}

int
nv50_bar_create(struct nouveau_device *ndev, int subdev)
{
	struct nv50_bar_priv *priv;
	struct nouveau_vm *vm;
	u64 pgdptr;
	int ret;

	ret = nouveau_subdev_create(ndev, subdev, "BAR", "bar1", &priv);
	if (ret)
		return ret;

	priv->base.base.destroy = nv50_bar_destroy;
	priv->base.base.init = nv50_bar_init;
	priv->base.map = nv50_bar_map;
	priv->base.unmap = nv50_bar_unmap;

	/* need to reuse the channel setup by INSTMEM, BAR1 (FB) is
	 * hardcoded to +0GiB in channel address space
	 */
	pgdptr = (u64)nv_rd32(ndev, 0x001704) << 12;
	if (ndev->chipset == 0x50)
		pgdptr += 0x1400;
	else
		pgdptr += 0x0200;

	ret = nouveau_gpuobj_new_fake(ndev, ~0, pgdptr, 0x4000, 0, &priv->pgd);
	if (ret)
		goto done;

	ret = nouveau_vm_new(ndev, 0, pci_resource_len(ndev->dev->pdev, 1),
			     0, &vm);
	if (ret)
		goto done;

	ret = nouveau_vm_ref(vm, &priv->vm, priv->pgd);
	nouveau_vm_ref(NULL, &vm, NULL);
	if (ret)
		goto done;

	nv_wr32(ndev, 0x704400, 0x7fc00000);
	nv_wr32(ndev, 0x704404, pci_resource_len(ndev->dev->pdev, 1) - 1);
	nv_wr32(ndev, 0x704408, 0x00000000);
	nv_wr32(ndev, 0x70440c, 0x00000000);
	nv_wr32(ndev, 0x704410, 0x00000000);
	nv_wr32(ndev, 0x704414, 0x00000000);

	/* create vm for kernel mappings, doesn't really belong here.. */
	ret = nouveau_vm_new(ndev, 0, (1ULL << 40), 0x0020000000ULL,
			    &ndev->chan_vm);
	if (ret)
		goto done;

done:
	return nouveau_subdev_init(ndev, subdev, ret);
}
