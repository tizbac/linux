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
#include "nouveau_fifo.h"
#include "nouveau_ramht.h"
#include "nouveau_gpuobj.h"
#include "nouveau_mpeg.h"

struct nv31_mpeg_priv {
	struct nouveau_mpeg_priv base;
	atomic_t refcount;
};

static int
nv31_mpeg_context_new(struct nouveau_channel *chan, int engine)
{
	struct nv31_mpeg_priv *priv = nv_engine(chan->device, engine);

	if (!atomic_add_unless(&priv->refcount, 1, 1))
		return -EBUSY;

	chan->engctx[engine] = (void *)0xdeadcafe;
	return 0;
}

static void
nv31_mpeg_context_del(struct nouveau_channel *chan, int engine)
{
	struct nv31_mpeg_priv *priv = nv_engine(chan->device, engine);
	atomic_dec(&priv->refcount);
	chan->engctx[engine] = NULL;
}

static int
nv40_mpeg_context_new(struct nouveau_channel *chan, int engine)
{
	struct nouveau_device *ndev = chan->device;
	struct nouveau_gpuobj *ctx = NULL;
	unsigned long flags;
	int ret;

	NV_DEBUG(ndev, "ch%d\n", chan->id);

	ret = nouveau_gpuobj_new(ndev, NULL, 264 * 4, 16, NVOBJ_FLAG_ZERO_ALLOC |
				 NVOBJ_FLAG_ZERO_FREE, &ctx);
	if (ret)
		return ret;

	nv_wo32(ctx, 0x78, 0x02001ec1);

	spin_lock_irqsave(&ndev->context_switch_lock, flags);
	nv_mask(ndev, 0x002500, 0x00000001, 0x00000000);
	if ((nv_rd32(ndev, 0x003204) & 0x1f) == chan->id)
		nv_wr32(ndev, 0x00330c, ctx->pinst >> 4);
	nv_wo32(chan->ramfc, 0x54, ctx->pinst >> 4);
	nv_mask(ndev, 0x002500, 0x00000001, 0x00000001);
	spin_unlock_irqrestore(&ndev->context_switch_lock, flags);

	chan->engctx[engine] = ctx;
	return 0;
}

static void
nv40_mpeg_context_del(struct nouveau_channel *chan, int engine)
{
	struct nouveau_gpuobj *ctx = chan->engctx[engine];
	struct nouveau_device *ndev = chan->device;
	unsigned long flags;
	u32 inst = 0x80000000 | (ctx->pinst >> 4);

	spin_lock_irqsave(&ndev->context_switch_lock, flags);
	nv_mask(ndev, 0x00b32c, 0x00000001, 0x00000000);
	if (nv_rd32(ndev, 0x00b318) == inst)
		nv_mask(ndev, 0x00b318, 0x80000000, 0x00000000);
	nv_mask(ndev, 0x00b32c, 0x00000001, 0x00000001);
	spin_unlock_irqrestore(&ndev->context_switch_lock, flags);

	nouveau_gpuobj_ref(NULL, &ctx);
	chan->engctx[engine] = NULL;
}

static int
nv31_mpeg_object_new(struct nouveau_channel *chan, int engine,
		      u32 handle, u16 class)
{
	struct nouveau_device *ndev = chan->device;
	struct nouveau_gpuobj *obj = NULL;
	int ret;

	ret = nouveau_gpuobj_new(ndev, chan, 20, 16, NVOBJ_FLAG_ZERO_ALLOC |
				 NVOBJ_FLAG_ZERO_FREE, &obj);
	if (ret)
		return ret;
	obj->engine = 2;
	obj->class  = class;

	nv_wo32(obj, 0x00, class);

	ret = nouveau_ramht_insert(chan, handle, obj);
	nouveau_gpuobj_ref(NULL, &obj);
	return ret;
}

static int
nv31_mpeg_init(struct nouveau_device *ndev, int engine)
{
	struct nouveau_fb *pfb = nv_subdev(ndev, NVDEV_SUBDEV_FB);
	struct nv31_mpeg_priv *priv = nv_engine(ndev, engine);
	int i;

	/* VPE init */
	nv_wr32(ndev, 0x00b0e0, 0x00000020); /* nvidia: rd 0x01, wr 0x20 */
	nv_wr32(ndev, 0x00b0e8, 0x00000020); /* nvidia: rd 0x01, wr 0x20 */

	for (i = 0; i < pfb->num_tiles; i++)
		priv->base.base.set_tile_region(ndev, i);

	/* PMPEG init */
	nv_wr32(ndev, 0x00b32c, 0x00000000);
	nv_wr32(ndev, 0x00b314, 0x00000100);
	nv_wr32(ndev, 0x00b220, nv44_graph_class(ndev) ? 0x00000044 : 0x00000031);
	nv_wr32(ndev, 0x00b300, 0x02001ec1);
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
nv31_mpeg_fini(struct nouveau_device *ndev, int engine, bool suspend)
{
	/*XXX: context save? */
	return 0;
}

static int
nv31_mpeg_mthd_dma(struct nouveau_channel *chan, u32 class, u32 mthd, u32 data)
{
	struct nouveau_device *ndev = chan->device;
	u32 inst = data << 4;
	u32 dma0 = nv_ri32(ndev, inst + 0);
	u32 dma1 = nv_ri32(ndev, inst + 4);
	u32 dma2 = nv_ri32(ndev, inst + 8);
	u32 base = (dma2 & 0xfffff000) | (dma0 >> 20);
	u32 size = dma1 + 1;

	/* only allow linear DMA objects */
	if (!(dma0 & 0x00002000))
		return -EINVAL;

	if (mthd == 0x0190) {
		/* DMA_CMD */
		nv_mask(ndev, 0x00b300, 0x00030000, (dma0 & 0x00030000));
		nv_wr32(ndev, 0x00b334, base);
		nv_wr32(ndev, 0x00b324, size);
	} else
	if (mthd == 0x01a0) {
		/* DMA_DATA */
		nv_mask(ndev, 0x00b300, 0x000c0000, (dma0 & 0x00030000) << 2);
		nv_wr32(ndev, 0x00b360, base);
		nv_wr32(ndev, 0x00b364, size);
	} else {
		/* DMA_IMAGE, VRAM only */
		if (dma0 & 0x000c0000)
			return -EINVAL;

		nv_wr32(ndev, 0x00b370, base);
		nv_wr32(ndev, 0x00b374, size);
	}

	return 0;
}

static int
nv31_mpeg_isr_chid(struct nouveau_device *ndev, u32 inst)
{
	struct nouveau_fifo_priv *pfifo = nv_engine(ndev, NVDEV_ENGINE_FIFO);
	struct nouveau_gpuobj *ctx;
	unsigned long flags;
	int i;

	/* hardcode drm channel id on nv3x, so swmthd lookup works */
	if (ndev->card_type < NV_40)
		return 0;

	spin_lock_irqsave(&ndev->channels.lock, flags);
	for (i = 0; i < pfifo->channels; i++) {
		if (!ndev->channels.ptr[i])
			continue;

		ctx = ndev->channels.ptr[i]->engctx[NVDEV_ENGINE_MPEG];
		if (ctx && ctx->pinst == inst)
			break;
	}
	spin_unlock_irqrestore(&ndev->channels.lock, flags);
	return i;
}

static void
nv31_vpe_set_tile_region(struct nouveau_device *ndev, int i)
{
	struct nouveau_tile_reg *tile = &ndev->tile.reg[i];

	nv_wr32(ndev, 0x00b008 + (i * 0x10), tile->pitch);
	nv_wr32(ndev, 0x00b004 + (i * 0x10), tile->limit);
	nv_wr32(ndev, 0x00b000 + (i * 0x10), tile->addr);
}

static void
nv31_mpeg_isr(struct nouveau_device *ndev)
{
	u32 inst = (nv_rd32(ndev, 0x00b318) & 0x000fffff) << 4;
	u32 chid = nv31_mpeg_isr_chid(ndev, inst);
	u32 stat = nv_rd32(ndev, 0x00b100);
	u32 type = nv_rd32(ndev, 0x00b230);
	u32 mthd = nv_rd32(ndev, 0x00b234);
	u32 data = nv_rd32(ndev, 0x00b238);
	u32 show = stat;

	if (stat & 0x01000000) {
		/* happens on initial binding of the object */
		if (type == 0x00000020 && mthd == 0x0000) {
			nv_mask(ndev, 0x00b308, 0x00000000, 0x00000000);
			show &= ~0x01000000;
		}

		if (type == 0x00000010) {
			if (!nouveau_gpuobj_mthd_call2(ndev, chid, 0x3174, mthd, data))
				show &= ~0x01000000;
		}
	}

	nv_wr32(ndev, 0x00b100, stat);
	nv_wr32(ndev, 0x00b230, 0x00000001);

	if (show && nouveau_ratelimit()) {
		NV_INFO(ndev, "PMPEG: Ch %d [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x\n",
			chid, inst, stat, type, mthd, data);
	}
}

static void
nv31_vpe_isr(struct nouveau_device *ndev)
{
	if (nv_rd32(ndev, 0x00b100))
		nv31_mpeg_isr(ndev);

	if (nv_rd32(ndev, 0x00b800)) {
		u32 stat = nv_rd32(ndev, 0x00b800);
		NV_INFO(ndev, "PMSRCH: 0x%08x\n", stat);
		nv_wr32(ndev, 0xb800, stat);
	}
}

static void
nv31_mpeg_destroy(struct nouveau_device *ndev, int engine)
{
	nouveau_irq_unregister(ndev, 0);
}

int
nv31_mpeg_create(struct nouveau_device *ndev, int engine)
{
	struct nv31_mpeg_priv *priv;
	int ret;

	ret = nouveau_engine_create(ndev, engine, "PMPEG", "mpeg", &priv);
	if (ret)
		return ret;

	atomic_set(&priv->refcount, 0);

	priv->base.base.base.destroy = nv31_mpeg_destroy;
	priv->base.base.base.init = nv31_mpeg_init;
	priv->base.base.base.fini = nv31_mpeg_fini;
	priv->base.base.base.unit = 0x00000002;
	if (ndev->card_type < NV_40) {
		priv->base.base.context_new = nv31_mpeg_context_new;
		priv->base.base.context_del = nv31_mpeg_context_del;
	} else {
		priv->base.base.context_new = nv40_mpeg_context_new;
		priv->base.base.context_del = nv40_mpeg_context_del;
	}
	priv->base.base.object_new = nv31_mpeg_object_new;

	/* ISR vector, PMC_ENABLE bit,  and TILE regs are shared between
	 * all VPE engines, for this driver's purposes the PMPEG engine
	 * will be treated as the "master" and handle the global VPE
	 * bits too
	 */
	priv->base.base.set_tile_region = nv31_vpe_set_tile_region;
	nouveau_irq_register(ndev, 0, nv31_vpe_isr);

	NVOBJ_CLASS(ndev, 0x3174, MPEG);
	NVOBJ_MTHD (ndev, 0x3174, 0x0190, nv31_mpeg_mthd_dma);
	NVOBJ_MTHD (ndev, 0x3174, 0x01a0, nv31_mpeg_mthd_dma);
	NVOBJ_MTHD (ndev, 0x3174, 0x01b0, nv31_mpeg_mthd_dma);

#if 0
	NVDEV_ENGINE_ADD(ndev, ME, &pme->base);
	NVOBJ_CLASS(ndev, 0x4075, ME);
#endif
	return nouveau_engine_init(ndev, engine, ret);
}
