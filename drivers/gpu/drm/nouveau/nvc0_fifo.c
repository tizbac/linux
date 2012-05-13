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
#include "nouveau_mm.h"
#include "nouveau_fifo.h"
#include "nouveau_instmem.h"
#include "nouveau_gpuobj.h"
#include "nouveau_bar.h"

static void nvc0_fifo_isr(struct nouveau_device *);

struct nvc0_fifo_priv {
	struct nouveau_fifo_priv base;
	struct nouveau_gpuobj *user;
	struct nouveau_gpuobj *playlist[2];
	int cur_playlist;
	int spoon_nr;
};

struct nvc0_fifo_chan {
	struct nouveau_fifo_chan base;
};

static void
nvc0_fifo_playlist_update(struct nouveau_device *ndev)
{
	struct nvc0_fifo_priv *priv = nv_engine(ndev, NVOBJ_ENGINE_FIFO);
	struct nouveau_gpuobj *cur;
	int i, p;

	cur = priv->playlist[priv->cur_playlist];
	priv->cur_playlist = !priv->cur_playlist;

	for (i = 0, p = 0; i < 128; i++) {
		if (!(nv_rd32(ndev, 0x3004 + (i * 8)) & 1))
			continue;
		nv_wo32(cur, p + 0, i);
		nv_wo32(cur, p + 4, 0x00000004);
		p += 8;
	}
	nouveau_instmem_flush(ndev);

	nv_wr32(ndev, 0x002270, cur->vinst >> 12);
	nv_wr32(ndev, 0x002274, 0x01f00000 | (p >> 3));
	if (!nv_wait(ndev, 0x00227c, 0x00100000, 0x00000000))
		NV_ERROR(ndev, "PFIFO - playlist update failed\n");
}

static int
nvc0_fifo_context_new(struct nouveau_channel *chan, int engine)
{
	struct nouveau_device *ndev = chan->device;
	struct nvc0_fifo_priv *priv = nv_engine(ndev, engine);
	struct nvc0_fifo_chan *fctx;
	u64 ib_virt = chan->pushbuf_base + chan->dma.ib_base * 4;
	u64 usermem = priv->user->vinst + chan->id * 0x1000;
	int ret = 0, i;

	fctx = chan->engctx[engine] = kzalloc(sizeof(*fctx), GFP_KERNEL);
	if (!fctx)
		return -ENOMEM;

	/* map control registers, and initialise them all to zero */
	chan->user = ioremap_wc(pci_resource_start(ndev->dev->pdev, 1) +
				(*(struct nouveau_mem **)priv->user->node)->
				bar_vma.offset + (chan->id * 0x1000), 0x1000);
	if (!chan->user) {
		ret = -ENOMEM;
		goto error;
	}

	for (i = 0; i < 0x1000; i += 4)
		nv_wo32(priv->user, (chan->id * 0x1000) + i, 0x00000000);

	/* initialise default fifo context */
	for (i = 0; i < 0x100; i += 4)
		nv_wo32(chan->ramin, i, 0x00000000);
	nv_wo32(chan->ramin, 0x08, lower_32_bits(usermem));
	nv_wo32(chan->ramin, 0x0c, upper_32_bits(usermem));
	nv_wo32(chan->ramin, 0x10, 0x0000face);
	nv_wo32(chan->ramin, 0x30, 0xfffff902);
	nv_wo32(chan->ramin, 0x48, lower_32_bits(ib_virt));
	nv_wo32(chan->ramin, 0x4c, drm_order(chan->dma.ib_max + 1) << 16 |
				   upper_32_bits(ib_virt));
	nv_wo32(chan->ramin, 0x54, 0x00000002);
	nv_wo32(chan->ramin, 0x84, 0x20400000);
	nv_wo32(chan->ramin, 0x94, 0x30000001);
	nv_wo32(chan->ramin, 0x9c, 0x00000100);
	nv_wo32(chan->ramin, 0xa4, 0x1f1f1f1f);
	nv_wo32(chan->ramin, 0xa8, 0x1f1f1f1f);
	nv_wo32(chan->ramin, 0xac, 0x0000001f);
	nv_wo32(chan->ramin, 0xb8, 0xf8000000);
	nv_wo32(chan->ramin, 0xf8, 0x10003080); /* 0x002310 */
	nv_wo32(chan->ramin, 0xfc, 0x10000010); /* 0x002350 */
	nouveau_instmem_flush(ndev);

	nv_wr32(ndev, 0x003000 + (chan->id * 8), 0xc0000000 |
						(chan->ramin->vinst >> 12));
	nv_wr32(ndev, 0x003004 + (chan->id * 8), 0x001f0001);
	nvc0_fifo_playlist_update(ndev);

error:
	if (ret)
		priv->base.base.context_del(chan, engine);
	return ret;
}

static void
nvc0_fifo_context_del(struct nouveau_channel *chan, int engine)
{
	struct nvc0_fifo_chan *fctx = chan->engctx[engine];
	struct nouveau_device *ndev = chan->device;

	nv_mask(ndev, 0x003004 + (chan->id * 8), 0x00000001, 0x00000000);
	nv_wr32(ndev, 0x002634, chan->id);
	if (!nv_wait(ndev, 0x0002634, 0xffffffff, chan->id))
		NV_WARN(ndev, "0x2634 != chid: 0x%08x\n", nv_rd32(ndev, 0x2634));
	nvc0_fifo_playlist_update(ndev);
	nv_wr32(ndev, 0x003000 + (chan->id * 8), 0x00000000);

	if (chan->user) {
		iounmap(chan->user);
		chan->user = NULL;
	}

	chan->engctx[engine] = NULL;
	kfree(fctx);
}

static int
nvc0_fifo_init(struct nouveau_device *ndev, int engine)
{
	struct nvc0_fifo_priv *priv = nv_engine(ndev, engine);
	struct nouveau_mem *user = *(struct nouveau_mem **)priv->user->node;
	struct nouveau_channel *chan;
	int i;

	/* reset PFIFO, enable all available PSUBFIFO areas */
	nv_mask(ndev, 0x000200, 0x00000100, 0x00000000);
	nv_mask(ndev, 0x000200, 0x00000100, 0x00000100);
	nv_wr32(ndev, 0x000204, 0xffffffff);
	nv_wr32(ndev, 0x002204, 0xffffffff);

	priv->spoon_nr = hweight32(nv_rd32(ndev, 0x002204));
	NV_DEBUG(ndev, "PFIFO: %d subfifo(s)\n", priv->spoon_nr);

	/* assign engines to subfifos */
	if (priv->spoon_nr >= 3) {
		nv_wr32(ndev, 0x002208, ~(1 << 0)); /* PGRAPH */
		nv_wr32(ndev, 0x00220c, ~(1 << 1)); /* PVP */
		nv_wr32(ndev, 0x002210, ~(1 << 1)); /* PPP */
		nv_wr32(ndev, 0x002214, ~(1 << 1)); /* PBSP */
		nv_wr32(ndev, 0x002218, ~(1 << 2)); /* PCE0 */
		nv_wr32(ndev, 0x00221c, ~(1 << 1)); /* PCE1 */
	}

	/* PSUBFIFO[n] */
	for (i = 0; i < priv->spoon_nr; i++) {
		nv_mask(ndev, 0x04013c + (i * 0x2000), 0x10000100, 0x00000000);
		nv_wr32(ndev, 0x040108 + (i * 0x2000), 0xffffffff); /* INTR */
		nv_wr32(ndev, 0x04010c + (i * 0x2000), 0xfffffeff); /* INTR_EN */
	}

	nv_mask(ndev, 0x002200, 0x00000001, 0x00000001);
	nv_wr32(ndev, 0x002254, 0x10000000 | user->bar_vma.offset >> 12);

	nv_wr32(ndev, 0x002a00, 0xffffffff); /* clears PFIFO.INTR bit 30 */
	nv_wr32(ndev, 0x002100, 0xffffffff);
	nv_wr32(ndev, 0x002140, 0xbfffffff);

	/* restore PFIFO context table */
	for (i = 0; i < 128; i++) {
		chan = ndev->channels.ptr[i];
		if (!chan || !chan->engctx[engine])
			continue;

		nv_wr32(ndev, 0x003000 + (i * 8), 0xc0000000 |
						 (chan->ramin->vinst >> 12));
		nv_wr32(ndev, 0x003004 + (i * 8), 0x001f0001);
	}
	nvc0_fifo_playlist_update(ndev);

	return 0;
}

static int
nvc0_fifo_fini(struct nouveau_device *ndev, int engine, bool suspend)
{
	int i;

	for (i = 0; i < 128; i++) {
		if (!(nv_rd32(ndev, 0x003004 + (i * 8)) & 1))
			continue;

		nv_mask(ndev, 0x003004 + (i * 8), 0x00000001, 0x00000000);
		nv_wr32(ndev, 0x002634, i);
		if (!nv_wait(ndev, 0x002634, 0xffffffff, i)) {
			NV_INFO(ndev, "PFIFO: kick ch %d failed: 0x%08x\n",
				i, nv_rd32(ndev, 0x002634));
			return -EBUSY;
		}
	}

	nv_wr32(ndev, 0x002140, 0x00000000);
	return 0;
}


struct nouveau_enum nvc0_fifo_fault_unit[] = {
	{ 0x00, "PGRAPH" },
	{ 0x03, "PEEPHOLE" },
	{ 0x04, "BAR1" },
	{ 0x05, "BAR3" },
	{ 0x07, "PFIFO" },
	{ 0x10, "PBSP" },
	{ 0x11, "PPPP" },
	{ 0x13, "PCOUNTER" },
	{ 0x14, "PVP" },
	{ 0x15, "PCOPY0" },
	{ 0x16, "PCOPY1" },
	{ 0x17, "PDAEMON" },
	{}
};

struct nouveau_enum nvc0_fifo_fault_reason[] = {
	{ 0x00, "PT_NOT_PRESENT" },
	{ 0x01, "PT_TOO_SHORT" },
	{ 0x02, "PAGE_NOT_PRESENT" },
	{ 0x03, "VM_LIMIT_EXCEEDED" },
	{ 0x04, "NO_CHANNEL" },
	{ 0x05, "PAGE_SYSTEM_ONLY" },
	{ 0x06, "PAGE_READ_ONLY" },
	{ 0x0a, "COMPRESSED_SYSRAM" },
	{ 0x0c, "INVALID_STORAGE_TYPE" },
	{}
};

struct nouveau_enum nvc0_fifo_fault_hubclient[] = {
	{ 0x01, "PCOPY0" },
	{ 0x02, "PCOPY1" },
	{ 0x04, "DISPATCH" },
	{ 0x05, "CTXCTL" },
	{ 0x06, "PFIFO" },
	{ 0x07, "BAR_READ" },
	{ 0x08, "BAR_WRITE" },
	{ 0x0b, "PVP" },
	{ 0x0c, "PPPP" },
	{ 0x0d, "PBSP" },
	{ 0x11, "PCOUNTER" },
	{ 0x12, "PDAEMON" },
	{ 0x14, "CCACHE" },
	{ 0x15, "CCACHE_POST" },
	{}
};

struct nouveau_enum nvc0_fifo_fault_gpcclient[] = {
	{ 0x01, "TEX" },
	{ 0x0c, "ESETUP" },
	{ 0x0e, "CTXCTL" },
	{ 0x0f, "PROP" },
	{}
};

struct nouveau_bitfield nvc0_fifo_subfifo_intr[] = {
/*	{ 0x00008000, "" }	seen with null ib push */
	{ 0x00200000, "ILLEGAL_MTHD" },
	{ 0x00800000, "EMPTY_SUBC" },
	{}
};

static void
nvc0_fifo_isr_vm_fault(struct nouveau_device *ndev, int unit)
{
	u32 inst = nv_rd32(ndev, 0x2800 + (unit * 0x10));
	u32 valo = nv_rd32(ndev, 0x2804 + (unit * 0x10));
	u32 vahi = nv_rd32(ndev, 0x2808 + (unit * 0x10));
	u32 stat = nv_rd32(ndev, 0x280c + (unit * 0x10));
	u32 client = (stat & 0x00001f00) >> 8;

	NV_INFO(ndev, "PFIFO: %s fault at 0x%010llx [",
		(stat & 0x00000080) ? "write" : "read", (u64)vahi << 32 | valo);
	nouveau_enum_print(nvc0_fifo_fault_reason, stat & 0x0000000f);
	printk("] from ");
	nouveau_enum_print(nvc0_fifo_fault_unit, unit);
	if (stat & 0x00000040) {
		printk("/");
		nouveau_enum_print(nvc0_fifo_fault_hubclient, client);
	} else {
		printk("/GPC%d/", (stat & 0x1f000000) >> 24);
		nouveau_enum_print(nvc0_fifo_fault_gpcclient, client);
	}
	printk(" on channel 0x%010llx\n", (u64)inst << 12);
}

static int
nvc0_fifo_page_flip(struct nouveau_device *ndev, u32 chid)
{
	struct nvc0_fifo_priv *priv = nv_engine(ndev, NVOBJ_ENGINE_FIFO);
	struct nouveau_channel *chan = NULL;
	unsigned long flags;
	int ret = -EINVAL;

	spin_lock_irqsave(&ndev->channels.lock, flags);
	if (likely(chid >= 0 && chid < priv->base.channels)) {
		chan = ndev->channels.ptr[chid];
		if (likely(chan))
			ret = nouveau_finish_page_flip(chan, NULL);
	}
	spin_unlock_irqrestore(&ndev->channels.lock, flags);
	return ret;
}

static void
nvc0_fifo_isr_subfifo_intr(struct nouveau_device *ndev, int unit)
{
	u32 stat = nv_rd32(ndev, 0x040108 + (unit * 0x2000));
	u32 addr = nv_rd32(ndev, 0x0400c0 + (unit * 0x2000));
	u32 data = nv_rd32(ndev, 0x0400c4 + (unit * 0x2000));
	u32 chid = nv_rd32(ndev, 0x040120 + (unit * 0x2000)) & 0x7f;
	u32 subc = (addr & 0x00070000);
	u32 mthd = (addr & 0x00003ffc);
	u32 show = stat;

	if (stat & 0x00200000) {
		if (mthd == 0x0054) {
			if (!nvc0_fifo_page_flip(ndev, chid))
				show &= ~0x00200000;
		}
	}

	if (show) {
		NV_INFO(ndev, "PFIFO%d:", unit);
		nouveau_bitfield_print(nvc0_fifo_subfifo_intr, show);
		NV_INFO(ndev, "PFIFO%d: ch %d subc %d mthd 0x%04x data 0x%08x\n",
			     unit, chid, subc, mthd, data);
	}

	nv_wr32(ndev, 0x0400c0 + (unit * 0x2000), 0x80600008);
	nv_wr32(ndev, 0x040108 + (unit * 0x2000), stat);
}

static void
nvc0_fifo_isr(struct nouveau_device *ndev)
{
	u32 stat = nv_rd32(ndev, 0x002100);

	if (stat & 0x00000100) {
		NV_INFO(ndev, "PFIFO: unknown status 0x00000100\n");
		nv_wr32(ndev, 0x002100, 0x00000100);
		stat &= ~0x00000100;
	}

	if (stat & 0x10000000) {
		u32 units = nv_rd32(ndev, 0x00259c);
		u32 u = units;

		while (u) {
			int i = ffs(u) - 1;
			nvc0_fifo_isr_vm_fault(ndev, i);
			u &= ~(1 << i);
		}

		nv_wr32(ndev, 0x00259c, units);
		stat &= ~0x10000000;
	}

	if (stat & 0x20000000) {
		u32 units = nv_rd32(ndev, 0x0025a0);
		u32 u = units;

		while (u) {
			int i = ffs(u) - 1;
			nvc0_fifo_isr_subfifo_intr(ndev, i);
			u &= ~(1 << i);
		}

		nv_wr32(ndev, 0x0025a0, units);
		stat &= ~0x20000000;
	}

	if (stat & 0x40000000) {
		NV_INFO(ndev, "PFIFO: unknown status 0x40000000\n");
		nv_mask(ndev, 0x002a00, 0x00000000, 0x00000000);
		stat &= ~0x40000000;
	}

	if (stat) {
		NV_INFO(ndev, "PFIFO: unhandled status 0x%08x\n", stat);
		nv_wr32(ndev, 0x002100, stat);
		nv_wr32(ndev, 0x002140, 0);
	}
}

static void
nvc0_fifo_destroy(struct nouveau_device *ndev, int engine)
{
	struct nvc0_fifo_priv *priv = nv_engine(ndev, NVOBJ_ENGINE_FIFO);
	struct nouveau_bar *pbar = nv_subdev(ndev, NVDEV_SUBDEV_BAR);

	if (priv->user) {
		pbar->unmap(pbar, *(struct nouveau_mem **)priv->user->node);
		nouveau_gpuobj_ref(NULL, &priv->user);
	}
	nouveau_gpuobj_ref(NULL, &priv->playlist[1]);
	nouveau_gpuobj_ref(NULL, &priv->playlist[0]);

	ndev->engine[engine] = NULL;
	kfree(priv);
}

int
nvc0_fifo_create(struct nouveau_device *ndev)
{
	struct nouveau_bar *pbar = nv_subdev(ndev, NVDEV_SUBDEV_BAR);
	struct nvc0_fifo_priv *priv;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base.base.destroy = nvc0_fifo_destroy;
	priv->base.base.init = nvc0_fifo_init;
	priv->base.base.fini = nvc0_fifo_fini;
	priv->base.base.context_new = nvc0_fifo_context_new;
	priv->base.base.context_del = nvc0_fifo_context_del;
	priv->base.channels = 128;
	ndev->engine[NVOBJ_ENGINE_FIFO] = &priv->base.base;

	ret = nouveau_gpuobj_new(ndev, NULL, 4096, 4096, 0, &priv->playlist[0]);
	if (ret)
		goto error;

	ret = nouveau_gpuobj_new(ndev, NULL, 4096, 4096, 0, &priv->playlist[1]);
	if (ret)
		goto error;

	ret = nouveau_gpuobj_new(ndev, NULL, priv->base.channels * 4096, 4096,
				 NVOBJ_FLAG_ZERO_ALLOC, &priv->user);
	if (ret)
		goto error;

	ret = pbar->map(pbar, *(struct nouveau_mem **)priv->user->node);
	if (ret)
		goto error;

	nouveau_irq_register(ndev, 8, nvc0_fifo_isr);
error:
	if (ret)
		priv->base.base.destroy(ndev, NVOBJ_ENGINE_FIFO);
	return ret;
}
