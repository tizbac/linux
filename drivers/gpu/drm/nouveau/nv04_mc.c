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
#include "nouveau_drm.h"
#include "nouveau_mc.h"

struct nv04_mc_priv {
	struct nouveau_mc base;
};

static int
nv04_mc_init(struct nouveau_device *ndev, int subdev)
{
	/* Power up everything, resetting each individual unit will
	 * be done later if needed.
	 */

	nv_wr32(ndev, NV03_PMC_ENABLE, 0xffffffff);

	/* Disable PROM access. */
	nv_wr32(ndev, NV_PBUS_PCI_NV_20, NV_PBUS_PCI_NV_20_ROM_SHADOW_ENABLED);

	return 0;
}

int
nv04_mc_create(struct nouveau_device *ndev, int subdev)
{
	struct nv04_mc_priv *priv;
	int ret;

	ret = nouveau_subdev_create(ndev, subdev, "PMC", "master", &priv);
	if (ret)
		return ret;

	priv->base.base.init = nv04_mc_init;
	return nouveau_subdev_init(ndev, subdev, ret);
}
