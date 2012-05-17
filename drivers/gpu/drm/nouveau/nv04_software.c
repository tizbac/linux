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
#include "nouveau_ramht.h"
#include "nouveau_fence.h"
#include "nouveau_software.h"
#include "nouveau_hw.h"
#include "nouveau_gpuobj.h"

struct nv04_software_priv {
	struct nouveau_software_priv base;
};

struct nv04_software_chan {
	struct nouveau_software_chan base;
};

static int
mthd_flip(struct nouveau_channel *chan, u32 class, u32 mthd, u32 data)
{

	struct nouveau_page_flip_state state;

	if (!nouveau_finish_page_flip(chan, &state)) {
		nv_set_crtc_base(chan->device, state.crtc, state.offset +
				 state.y * state.pitch +
				 state.x * state.bpp / 8);
	}

	return 0;
}

static int
nv04_software_context_new(struct nouveau_channel *chan, int engine)
{
	struct nv04_software_chan *pch;

	pch = kzalloc(sizeof(*pch), GFP_KERNEL);
	if (!pch)
		return -ENOMEM;

	nouveau_software_context_new(&pch->base);
	chan->engctx[engine] = pch;
	return 0;
}

static void
nv04_software_context_del(struct nouveau_channel *chan, int engine)
{
	struct nv04_software_chan *pch = chan->engctx[engine];
	chan->engctx[engine] = NULL;
	kfree(pch);
}

static int
nv04_software_object_new(struct nouveau_channel *chan, int engine,
			 u32 handle, u16 class)
{
	struct nouveau_device *ndev = chan->device;
	struct nouveau_gpuobj *obj = NULL;
	int ret;

	ret = nouveau_gpuobj_new(ndev, chan, 16, 16, 0, &obj);
	if (ret)
		return ret;
	obj->engine = 0;
	obj->class  = class;

	ret = nouveau_ramht_insert(chan, handle, obj);
	nouveau_gpuobj_ref(NULL, &obj);
	return ret;
}

int
nv04_software_create(struct nouveau_device *ndev, int engine)
{
	struct nv04_software_priv *priv;
	int ret;

	ret = nouveau_engine_create(ndev, engine, "SW", "software", &priv);
	if (ret)
		return ret;

	priv->base.base.context_new = nv04_software_context_new;
	priv->base.base.context_del = nv04_software_context_del;
	priv->base.base.object_new = nv04_software_object_new;
	nouveau_software_create(&priv->base);

	if (ndev->card_type <= NV_04) {
		NVOBJ_CLASS(ndev, 0x006e, SW);
		NVOBJ_MTHD (ndev, 0x006e, 0x0150, nv04_fence_mthd);
		NVOBJ_MTHD (ndev, 0x006e, 0x0500, mthd_flip);
	} else {
		NVOBJ_CLASS(ndev, 0x016e, SW);
		NVOBJ_MTHD (ndev, 0x016e, 0x0500, mthd_flip);
	}

	return nouveau_engine_init(ndev, engine, ret);
}
