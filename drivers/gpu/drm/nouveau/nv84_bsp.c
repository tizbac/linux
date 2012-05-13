/*
 * Copyright 2011 Red Hat Inc.
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
#include "nouveau_util.h"
#include "nouveau_vm.h"
#include "nouveau_ramht.h"
#include "nouveau_gpuobj.h"

/*XXX: This stub is currently used on NV98+ also, as soon as this becomes
 *     more than just an enable/disable stub this needs to be split out to
 *     nv98_bsp.c...
 */

struct nv84_bsp_engine {
	struct nouveau_engine base;
};

static int
nv84_bsp_fini(struct nouveau_device *ndev, int engine, bool suspend)
{
	if (!(nv_rd32(ndev, 0x000200) & 0x00008000))
		return 0;

	nv_mask(ndev, 0x000200, 0x00008000, 0x00000000);
	return 0;
}

static int
nv84_bsp_init(struct nouveau_device *ndev, int engine)
{
	nv_mask(ndev, 0x000200, 0x00008000, 0x00000000);
	nv_mask(ndev, 0x000200, 0x00008000, 0x00008000);
	return 0;
}

static void
nv84_bsp_destroy(struct nouveau_device *ndev, int engine)
{
	struct nv84_bsp_engine *pbsp = nv_engine(ndev, engine);

	NVOBJ_ENGINE_DEL(ndev, BSP);

	kfree(pbsp);
}

int
nv84_bsp_create(struct nouveau_device *ndev)
{
	struct nv84_bsp_engine *pbsp;

	pbsp = kzalloc(sizeof(*pbsp), GFP_KERNEL);
	if (!pbsp)
		return -ENOMEM;

	pbsp->base.destroy = nv84_bsp_destroy;
	pbsp->base.init = nv84_bsp_init;
	pbsp->base.fini = nv84_bsp_fini;

	NVOBJ_ENGINE_ADD(ndev, BSP, &pbsp->base);
	return 0;
}
