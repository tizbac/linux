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

/*XXX: This stub is currently used on NV98+ also, as soon as this becomes
 *     more than just an enable/disable stub this needs to be split out to
 *     nv98_vp.c...
 */

struct nv84_vp_engine {
	struct nouveau_engine base;
};

static int
nv84_vp_fini(struct nouveau_device *ndev, int engine, bool suspend)
{
	if (!(nv_rd32(ndev, 0x000200) & 0x00020000))
		return 0;

	nv_mask(ndev, 0x000200, 0x00020000, 0x00000000);
	return 0;
}

static int
nv84_vp_init(struct nouveau_device *ndev, int engine)
{
	nv_mask(ndev, 0x000200, 0x00020000, 0x00000000);
	nv_mask(ndev, 0x000200, 0x00020000, 0x00020000);
	return 0;
}

static void
nv84_vp_destroy(struct nouveau_device *ndev, int engine)
{
	struct nv84_vp_engine *pvp = nv_engine(ndev, engine);

	NVOBJ_ENGINE_DEL(ndev, VP);

	kfree(pvp);
}

int
nv84_vp_create(struct nouveau_device *ndev)
{
	struct nv84_vp_engine *pvp;

	pvp = kzalloc(sizeof(*pvp), GFP_KERNEL);
	if (!pvp)
		return -ENOMEM;

	pvp->base.destroy = nv84_vp_destroy;
	pvp->base.init = nv84_vp_init;
	pvp->base.fini = nv84_vp_fini;

	NVOBJ_ENGINE_ADD(ndev, VP, &pvp->base);
	return 0;
}
