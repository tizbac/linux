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
#include "nouveau_bsp.h"

struct nvc0_bsp_priv {
	struct nouveau_bsp_priv base;
};

int
nvc0_bsp_create(struct nouveau_device *ndev, int engine)
{
	struct nvc0_bsp_priv *priv;
	int ret;

	ret = nouveau_engine_create(ndev, engine, "PBSP", "bsp", &priv);
	if (ret)
		return ret;

	priv->base.base.base.unit = 0x00008000;
	return nouveau_engine_init(ndev, engine, ret);
}
