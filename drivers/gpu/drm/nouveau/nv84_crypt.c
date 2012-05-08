/*
 * Copyright 2010 Red Hat Inc.
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

struct nv84_crypt_engine {
	struct nouveau_engine base;
};

static int
nv84_crypt_context_new(struct nouveau_channel *chan, int engine)
{
	struct nouveau_device *ndev = chan->device;
	struct nouveau_gpuobj *ramin = chan->ramin;
	struct nouveau_gpuobj *ctx;
	int ret;

	NV_DEBUG(ndev, "ch%d\n", chan->id);

	ret = nouveau_gpuobj_new(ndev, chan, 256, 0, NVOBJ_FLAG_ZERO_ALLOC |
				 NVOBJ_FLAG_ZERO_FREE, &ctx);
	if (ret)
		return ret;

	nv_wo32(ramin, 0xa0, 0x00190000);
	nv_wo32(ramin, 0xa4, ctx->vinst + ctx->size - 1);
	nv_wo32(ramin, 0xa8, ctx->vinst);
	nv_wo32(ramin, 0xac, 0);
	nv_wo32(ramin, 0xb0, 0);
	nv_wo32(ramin, 0xb4, 0);
	ndev->subsys.instmem.flush(ndev);

	atomic_inc(&chan->vm->engref[engine]);
	chan->engctx[engine] = ctx;
	return 0;
}

static void
nv84_crypt_context_del(struct nouveau_channel *chan, int engine)
{
	struct nouveau_gpuobj *ctx = chan->engctx[engine];
	struct nouveau_device *ndev = chan->device;
	u32 inst;

	inst  = (chan->ramin->vinst >> 12);
	inst |= 0x80000000;

	/* mark context as invalid if still on the hardware, not
	 * doing this causes issues the next time PCRYPT is used,
	 * unsurprisingly :)
	 */
	nv_wr32(ndev, 0x10200c, 0x00000000);
	if (nv_rd32(ndev, 0x102188) == inst)
		nv_mask(ndev, 0x102188, 0x80000000, 0x00000000);
	if (nv_rd32(ndev, 0x10218c) == inst)
		nv_mask(ndev, 0x10218c, 0x80000000, 0x00000000);
	nv_wr32(ndev, 0x10200c, 0x00000010);

	nouveau_gpuobj_ref(NULL, &ctx);

	atomic_dec(&chan->vm->engref[engine]);
	chan->engctx[engine] = NULL;
}

static int
nv84_crypt_object_new(struct nouveau_channel *chan, int engine,
		      u32 handle, u16 class)
{
	struct nouveau_device *ndev = chan->device;
	struct nouveau_gpuobj *obj = NULL;
	int ret;

	ret = nouveau_gpuobj_new(ndev, chan, 16, 16, NVOBJ_FLAG_ZERO_FREE, &obj);
	if (ret)
		return ret;
	obj->engine = 5;
	obj->class  = class;

	nv_wo32(obj, 0x00, class);
	ndev->subsys.instmem.flush(ndev);

	ret = nouveau_ramht_insert(chan, handle, obj);
	nouveau_gpuobj_ref(NULL, &obj);
	return ret;
}

static void
nv84_crypt_tlb_flush(struct nouveau_device *ndev, int engine)
{
	nv50_vm_flush_engine(ndev, 0x0a);
}

static void
nv84_crypt_isr(struct nouveau_device *ndev)
{
	u32 stat = nv_rd32(ndev, 0x102130);
	u32 mthd = nv_rd32(ndev, 0x102190);
	u32 data = nv_rd32(ndev, 0x102194);
	u32 inst = nv_rd32(ndev, 0x102188) & 0x7fffffff;
	int show = nouveau_ratelimit();

	if (show) {
		NV_INFO(ndev, "PCRYPT_INTR: 0x%08x 0x%08x 0x%08x 0x%08x\n",
			     stat, mthd, data, inst);
	}

	nv_wr32(ndev, 0x102130, stat);
	nv_wr32(ndev, 0x10200c, 0x10);

	nv50_fb_vm_trap(ndev, show);
}

static int
nv84_crypt_fini(struct nouveau_device *ndev, int engine, bool suspend)
{
	nv_wr32(ndev, 0x102140, 0x00000000);
	return 0;
}

static int
nv84_crypt_init(struct nouveau_device *ndev, int engine)
{
	nv_mask(ndev, 0x000200, 0x00004000, 0x00000000);
	nv_mask(ndev, 0x000200, 0x00004000, 0x00004000);

	nv_wr32(ndev, 0x102130, 0xffffffff);
	nv_wr32(ndev, 0x102140, 0xffffffbf);

	nv_wr32(ndev, 0x10200c, 0x00000010);
	return 0;
}

static void
nv84_crypt_destroy(struct nouveau_device *ndev, int engine)
{
	struct nv84_crypt_engine *pcrypt = nv_engine(ndev, engine);

	NVOBJ_ENGINE_DEL(ndev, CRYPT);

	nouveau_irq_unregister(ndev, 14);
	kfree(pcrypt);
}

int
nv84_crypt_create(struct nouveau_device *ndev)
{
	struct nv84_crypt_engine *pcrypt;

	pcrypt = kzalloc(sizeof(*pcrypt), GFP_KERNEL);
	if (!pcrypt)
		return -ENOMEM;

	pcrypt->base.destroy = nv84_crypt_destroy;
	pcrypt->base.init = nv84_crypt_init;
	pcrypt->base.fini = nv84_crypt_fini;
	pcrypt->base.context_new = nv84_crypt_context_new;
	pcrypt->base.context_del = nv84_crypt_context_del;
	pcrypt->base.object_new = nv84_crypt_object_new;
	pcrypt->base.tlb_flush = nv84_crypt_tlb_flush;

	nouveau_irq_register(ndev, 14, nv84_crypt_isr);

	NVOBJ_ENGINE_ADD(ndev, CRYPT, &pcrypt->base);
	NVOBJ_CLASS (ndev, 0x74c1, CRYPT);
	return 0;
}
