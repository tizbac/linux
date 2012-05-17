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
#include "nouveau_fb.h"
#include "nouveau_ramht.h"
#include "nouveau_instmem.h"
#include "nouveau_gpuobj.h"
#include "nouveau_mpeg.h"

struct nv50_mpeg_priv {
	struct nouveau_mpeg_priv base;
};

static inline u32
CTX_PTR(struct nouveau_device *ndev, u32 offset)
{
	if (ndev->chipset == 0x50)
		offset += 0x0260;
	else
		offset += 0x0060;

	return offset;
}

static int
nv50_mpeg_context_new(struct nouveau_channel *chan, int engine)
{
	struct nouveau_device *ndev = chan->device;
	struct nouveau_gpuobj *ramin = chan->ramin;
	struct nouveau_gpuobj *ctx = NULL;
	int ret;

	NV_DEBUG(ndev, "ch%d\n", chan->id);

	ret = nouveau_gpuobj_new(ndev, chan, 128 * 4, 0, NVOBJ_FLAG_ZERO_ALLOC |
				 NVOBJ_FLAG_ZERO_FREE, &ctx);
	if (ret)
		return ret;

	nv_wo32(ramin, CTX_PTR(ndev, 0x00), 0x80190002);
	nv_wo32(ramin, CTX_PTR(ndev, 0x04), ctx->vinst + ctx->size - 1);
	nv_wo32(ramin, CTX_PTR(ndev, 0x08), ctx->vinst);
	nv_wo32(ramin, CTX_PTR(ndev, 0x0c), 0);
	nv_wo32(ramin, CTX_PTR(ndev, 0x10), 0);
	nv_wo32(ramin, CTX_PTR(ndev, 0x14), 0x00010000);

	nv_wo32(ctx, 0x70, 0x00801ec1);
	nv_wo32(ctx, 0x7c, 0x0000037c);
	nouveau_instmem_flush(ndev);

	chan->engctx[engine] = ctx;
	return 0;
}

static void
nv50_mpeg_context_del(struct nouveau_channel *chan, int engine)
{
	struct nouveau_gpuobj *ctx = chan->engctx[engine];
	struct nouveau_device *ndev = chan->device;
	int i;

	for (i = 0x00; i <= 0x14; i += 4)
		nv_wo32(chan->ramin, CTX_PTR(ndev, i), 0x00000000);

	nouveau_gpuobj_ref(NULL, &ctx);
	chan->engctx[engine] = NULL;
}

static int
nv50_mpeg_object_new(struct nouveau_channel *chan, int engine,
		     u32 handle, u16 class)
{
	struct nouveau_device *ndev = chan->device;
	struct nouveau_gpuobj *obj = NULL;
	int ret;

	ret = nouveau_gpuobj_new(ndev, chan, 16, 16, NVOBJ_FLAG_ZERO_FREE, &obj);
	if (ret)
		return ret;
	obj->engine = 2;
	obj->class  = class;

	nv_wo32(obj, 0x00, class);
	nv_wo32(obj, 0x04, 0x00000000);
	nv_wo32(obj, 0x08, 0x00000000);
	nv_wo32(obj, 0x0c, 0x00000000);
	nouveau_instmem_flush(ndev);

	ret = nouveau_ramht_insert(chan, handle, obj);
	nouveau_gpuobj_ref(NULL, &obj);
	return ret;
}

static void
nv50_mpeg_tlb_flush(struct nouveau_device *ndev, int engine)
{
	nv50_vm_flush_engine(ndev, 0x08);
}

static int
nv50_mpeg_init(struct nouveau_device *ndev, int engine)
{
	nv_wr32(ndev, 0x00b32c, 0x00000000);
	nv_wr32(ndev, 0x00b314, 0x00000100);
	nv_wr32(ndev, 0x00b0e0, 0x0000001a);

	nv_wr32(ndev, 0x00b220, 0x00000044);
	nv_wr32(ndev, 0x00b300, 0x00801ec1);
	nv_wr32(ndev, 0x00b390, 0x00000000);
	nv_wr32(ndev, 0x00b394, 0x00000000);
	nv_wr32(ndev, 0x00b398, 0x00000000);
	nv_mask(ndev, 0x00b32c, 0x00000001, 0x00000001);

	nv_wr32(ndev, 0x00b100, 0xffffffff);
	nv_wr32(ndev, 0x00b140, 0xffffffff);

	if (!nv_wait(ndev, 0x00b200, 0x00000001, 0x00000000)) {
		NV_ERROR(ndev, "PMPEG init: 0x%08x\n", nv_rd32(ndev, 0x00b200));
		return -EBUSY;
	}

	return 0;
}

static int
nv50_mpeg_fini(struct nouveau_device *ndev, int engine, bool suspend)
{
	nv_mask(ndev, 0x00b32c, 0x00000001, 0x00000000);
	nv_wr32(ndev, 0x00b140, 0x00000000);
	return 0;
}

static void
nv50_mpeg_isr(struct nouveau_device *ndev)
{
	u32 stat = nv_rd32(ndev, 0x00b100);
	u32 type = nv_rd32(ndev, 0x00b230);
	u32 mthd = nv_rd32(ndev, 0x00b234);
	u32 data = nv_rd32(ndev, 0x00b238);
	u32 show = stat;

	if (stat & 0x01000000) {
		/* happens on initial binding of the object */
		if (type == 0x00000020 && mthd == 0x0000) {
			nv_wr32(ndev, 0x00b308, 0x00000100);
			show &= ~0x01000000;
		}
	}

	if (show && nouveau_ratelimit()) {
		NV_INFO(ndev, "PMPEG - 0x%08x 0x%08x 0x%08x 0x%08x\n",
			stat, type, mthd, data);
	}

	nv_wr32(ndev, 0x00b100, stat);
	nv_wr32(ndev, 0x00b230, 0x00000001);
	nv50_fb_vm_trap(ndev, 1);
}

static void
nv50_vpe_isr(struct nouveau_device *ndev)
{
	if (nv_rd32(ndev, 0x00b100))
		nv50_mpeg_isr(ndev);

	if (nv_rd32(ndev, 0x00b800)) {
		u32 stat = nv_rd32(ndev, 0x00b800);
		NV_INFO(ndev, "PMSRCH: 0x%08x\n", stat);
		nv_wr32(ndev, 0xb800, stat);
	}
}

static void
nv50_mpeg_destroy(struct nouveau_device *ndev, int engine)
{
	nouveau_irq_unregister(ndev, 0);
}

int
nv50_mpeg_create(struct nouveau_device *ndev, int engine)
{
	struct nv50_mpeg_priv *priv;
	int ret;

	ret = nouveau_engine_create(ndev, engine, "PMPEG", "mpeg", &priv);
	if (ret)
		return ret;

	priv->base.base.subdev.destroy = nv50_mpeg_destroy;
	priv->base.base.subdev.init = nv50_mpeg_init;
	priv->base.base.subdev.fini = nv50_mpeg_fini;
	priv->base.base.context_new = nv50_mpeg_context_new;
	priv->base.base.context_del = nv50_mpeg_context_del;
	priv->base.base.object_new = nv50_mpeg_object_new;
	priv->base.base.tlb_flush = nv50_mpeg_tlb_flush;

	if (ndev->chipset == 0x50) {
		nouveau_irq_register(ndev, 0, nv50_vpe_isr);
		NVOBJ_CLASS(ndev, 0x3174, MPEG);
#if 0
		NVDEV_ENGINE_ADD(ndev, ME, &pme->base);
		NVOBJ_CLASS(ndev, 0x4075, ME);
#endif
	} else {
		nouveau_irq_register(ndev, 0, nv50_mpeg_isr);
		NVOBJ_CLASS(ndev, 0x8274, MPEG);
	}

	return nouveau_engine_init(ndev, engine, ret);

}
