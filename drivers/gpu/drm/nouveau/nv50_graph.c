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
#include "nouveau_fb.h"
#include "nouveau_fifo.h"
#include "nouveau_ramht.h"
#include "nouveau_dma.h"
#include "nouveau_vm.h"
#include "nouveau_timer.h"
#include "nouveau_instmem.h"
#include "nouveau_gpuobj.h"

#include "nv50_evo.h"

struct nv50_graph_engine {
	struct nouveau_engine base;
	u32 ctxprog[512];
	u32 ctxprog_size;
	u32 grctx_size;
};

static int
nv50_graph_init(struct nouveau_device *ndev, int engine)
{
	struct nv50_graph_engine *pgraph = nv_engine(ndev, engine);
	u32 units = nv_rd32(ndev, 0x001540);
	int i;

	NV_DEBUG(ndev, "\n");

	/* master reset */
	nv_mask(ndev, 0x000200, 0x00201000, 0x00000000);
	nv_mask(ndev, 0x000200, 0x00201000, 0x00201000);
	nv_wr32(ndev, 0x40008c, 0x00000004); /* HW_CTX_SWITCH_ENABLED */

	/* reset/enable traps and interrupts */
	nv_wr32(ndev, 0x400804, 0xc0000000);
	nv_wr32(ndev, 0x406800, 0xc0000000);
	nv_wr32(ndev, 0x400c04, 0xc0000000);
	nv_wr32(ndev, 0x401800, 0xc0000000);
	nv_wr32(ndev, 0x405018, 0xc0000000);
	nv_wr32(ndev, 0x402000, 0xc0000000);
	for (i = 0; i < 16; i++) {
		if (!(units & (1 << i)))
			continue;

		if (ndev->chipset < 0xa0) {
			nv_wr32(ndev, 0x408900 + (i << 12), 0xc0000000);
			nv_wr32(ndev, 0x408e08 + (i << 12), 0xc0000000);
			nv_wr32(ndev, 0x408314 + (i << 12), 0xc0000000);
		} else {
			nv_wr32(ndev, 0x408600 + (i << 11), 0xc0000000);
			nv_wr32(ndev, 0x408708 + (i << 11), 0xc0000000);
			nv_wr32(ndev, 0x40831c + (i << 11), 0xc0000000);
		}
	}

	nv_wr32(ndev, 0x400108, 0xffffffff);
	nv_wr32(ndev, 0x400138, 0xffffffff);
	nv_wr32(ndev, 0x400100, 0xffffffff);
	nv_wr32(ndev, 0x40013c, 0xffffffff);
	nv_wr32(ndev, 0x400500, 0x00010001);

	/* upload context program, initialise ctxctl defaults */
	nv_wr32(ndev, 0x400324, 0x00000000);
	for (i = 0; i < pgraph->ctxprog_size; i++)
		nv_wr32(ndev, 0x400328, pgraph->ctxprog[i]);
	nv_wr32(ndev, 0x400824, 0x00000000);
	nv_wr32(ndev, 0x400828, 0x00000000);
	nv_wr32(ndev, 0x40082c, 0x00000000);
	nv_wr32(ndev, 0x400830, 0x00000000);
	nv_wr32(ndev, 0x400724, 0x00000000);
	nv_wr32(ndev, 0x40032c, 0x00000000);
	nv_wr32(ndev, 0x400320, 4);	/* CTXCTL_CMD = NEWCTXDMA */

	/* some unknown zcull magic */
	switch (ndev->chipset & 0xf0) {
	case 0x50:
	case 0x80:
	case 0x90:
		nv_wr32(ndev, 0x402ca8, 0x00000800);
		break;
	case 0xa0:
	default:
		nv_wr32(ndev, 0x402cc0, 0x00000000);
		if (ndev->chipset == 0xa0 ||
		    ndev->chipset == 0xaa ||
		    ndev->chipset == 0xac) {
			nv_wr32(ndev, 0x402ca8, 0x00000802);
		} else {
			nv_wr32(ndev, 0x402cc0, 0x00000000);
			nv_wr32(ndev, 0x402ca8, 0x00000002);
		}

		break;
	}

	/* zero out zcull regions */
	for (i = 0; i < 8; i++) {
		nv_wr32(ndev, 0x402c20 + (i * 8), 0x00000000);
		nv_wr32(ndev, 0x402c24 + (i * 8), 0x00000000);
		nv_wr32(ndev, 0x402c28 + (i * 8), 0x00000000);
		nv_wr32(ndev, 0x402c2c + (i * 8), 0x00000000);
	}

	return 0;
}

static int
nv50_graph_fini(struct nouveau_device *ndev, int engine, bool suspend)
{
	nv_wr32(ndev, 0x40013c, 0x00000000);
	return 0;
}

static int
nv50_graph_context_new(struct nouveau_channel *chan, int engine)
{
	struct nouveau_device *ndev = chan->device;
	struct nouveau_gpuobj *ramin = chan->ramin;
	struct nouveau_gpuobj *grctx = NULL;
	struct nv50_graph_engine *pgraph = nv_engine(ndev, engine);
	int hdr, ret;

	NV_DEBUG(ndev, "ch%d\n", chan->id);

	ret = nouveau_gpuobj_new(ndev, NULL, pgraph->grctx_size, 0,
				 NVOBJ_FLAG_ZERO_ALLOC |
				 NVOBJ_FLAG_ZERO_FREE, &grctx);
	if (ret)
		return ret;

	hdr = (ndev->chipset == 0x50) ? 0x200 : 0x20;
	nv_wo32(ramin, hdr + 0x00, 0x00190002);
	nv_wo32(ramin, hdr + 0x04, grctx->vinst + grctx->size - 1);
	nv_wo32(ramin, hdr + 0x08, grctx->vinst);
	nv_wo32(ramin, hdr + 0x0c, 0);
	nv_wo32(ramin, hdr + 0x10, 0);
	nv_wo32(ramin, hdr + 0x14, 0x00010000);

	nv50_grctx_fill(ndev, grctx);
	nv_wo32(grctx, 0x00000, chan->ramin->vinst >> 12);

	nouveau_instmem_flush(ndev);

	atomic_inc(&chan->vm->engref[NVOBJ_ENGINE_GR]);
	chan->engctx[NVOBJ_ENGINE_GR] = grctx;
	return 0;
}

static void
nv50_graph_context_del(struct nouveau_channel *chan, int engine)
{
	struct nouveau_gpuobj *grctx = chan->engctx[engine];
	struct nouveau_device *ndev = chan->device;
	int i, hdr = (ndev->chipset == 0x50) ? 0x200 : 0x20;

	for (i = hdr; i < hdr + 24; i += 4)
		nv_wo32(chan->ramin, i, 0);
	nouveau_instmem_flush(ndev);

	atomic_dec(&chan->vm->engref[engine]);
	nouveau_gpuobj_ref(NULL, &grctx);
	chan->engctx[engine] = NULL;
}

static int
nv50_graph_object_new(struct nouveau_channel *chan, int engine,
		      u32 handle, u16 class)
{
	struct nouveau_device *ndev = chan->device;
	struct nouveau_gpuobj *obj = NULL;
	int ret;

	ret = nouveau_gpuobj_new(ndev, chan, 16, 16, NVOBJ_FLAG_ZERO_FREE, &obj);
	if (ret)
		return ret;
	obj->engine = 1;
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
nv50_graph_tlb_flush(struct nouveau_device *ndev, int engine)
{
	nv50_vm_flush_engine(ndev, 0);
}

static void
nv84_graph_tlb_flush(struct nouveau_device *ndev, int engine)
{
	struct nouveau_timer *ptimer = nv_subdev(ndev, NVDEV_SUBDEV_TIMER);
	bool idle, timeout = false;
	unsigned long flags;
	u64 start;
	u32 tmp;

	spin_lock_irqsave(&ndev->context_switch_lock, flags);
	nv_mask(ndev, 0x400500, 0x00000001, 0x00000000);

	start = ptimer->read(ptimer);
	do {
		idle = true;

		for (tmp = nv_rd32(ndev, 0x400380); tmp && idle; tmp >>= 3) {
			if ((tmp & 7) == 1)
				idle = false;
		}

		for (tmp = nv_rd32(ndev, 0x400384); tmp && idle; tmp >>= 3) {
			if ((tmp & 7) == 1)
				idle = false;
		}

		for (tmp = nv_rd32(ndev, 0x400388); tmp && idle; tmp >>= 3) {
			if ((tmp & 7) == 1)
				idle = false;
		}
	} while (!idle &&
		 !(timeout = ptimer->read(ptimer) - start > 2000000000));

	if (timeout) {
		NV_ERROR(ndev, "PGRAPH TLB flush idle timeout fail: "
			      "0x%08x 0x%08x 0x%08x 0x%08x\n",
			 nv_rd32(ndev, 0x400700), nv_rd32(ndev, 0x400380),
			 nv_rd32(ndev, 0x400384), nv_rd32(ndev, 0x400388));
	}

	nv50_vm_flush_engine(ndev, 0);

	nv_mask(ndev, 0x400500, 0x00000001, 0x00000001);
	spin_unlock_irqrestore(&ndev->context_switch_lock, flags);
}

static struct nouveau_enum nv50_mp_exec_error_names[] = {
	{ 3, "STACK_UNDERFLOW", NULL },
	{ 4, "QUADON_ACTIVE", NULL },
	{ 8, "TIMEOUT", NULL },
	{ 0x10, "INVALID_OPCODE", NULL },
	{ 0x40, "BREAKPOINT", NULL },
	{}
};

static struct nouveau_bitfield nv50_graph_trap_m2mf[] = {
	{ 0x00000001, "NOTIFY" },
	{ 0x00000002, "IN" },
	{ 0x00000004, "OUT" },
	{}
};

static struct nouveau_bitfield nv50_graph_trap_vfetch[] = {
	{ 0x00000001, "FAULT" },
	{}
};

static struct nouveau_bitfield nv50_graph_trap_strmout[] = {
	{ 0x00000001, "FAULT" },
	{}
};

static struct nouveau_bitfield nv50_graph_trap_ccache[] = {
	{ 0x00000001, "FAULT" },
	{}
};

/* There must be a *lot* of these. Will take some time to gather them up. */
struct nouveau_enum nv50_data_error_names[] = {
	{ 0x00000003, "INVALID_QUERY_OR_TEXTURE", NULL },
	{ 0x00000004, "INVALID_VALUE", NULL },
	{ 0x00000005, "INVALID_ENUM", NULL },
	{ 0x00000008, "INVALID_OBJECT", NULL },
	{ 0x00000009, "READ_ONLY_OBJECT", NULL },
	{ 0x0000000a, "SUPERVISOR_OBJECT", NULL },
	{ 0x0000000b, "INVALID_ADDRESS_ALIGNMENT", NULL },
	{ 0x0000000c, "INVALID_BITFIELD", NULL },
	{ 0x0000000d, "BEGIN_END_ACTIVE", NULL },
	{ 0x0000000e, "SEMANTIC_COLOR_BACK_OVER_LIMIT", NULL },
	{ 0x0000000f, "VIEWPORT_ID_NEEDS_GP", NULL },
	{ 0x00000010, "RT_DOUBLE_BIND", NULL },
	{ 0x00000011, "RT_TYPES_MISMATCH", NULL },
	{ 0x00000012, "RT_LINEAR_WITH_ZETA", NULL },
	{ 0x00000015, "FP_TOO_FEW_REGS", NULL },
	{ 0x00000016, "ZETA_FORMAT_CSAA_MISMATCH", NULL },
	{ 0x00000017, "RT_LINEAR_WITH_MSAA", NULL },
	{ 0x00000018, "FP_INTERPOLANT_START_OVER_LIMIT", NULL },
	{ 0x00000019, "SEMANTIC_LAYER_OVER_LIMIT", NULL },
	{ 0x0000001a, "RT_INVALID_ALIGNMENT", NULL },
	{ 0x0000001b, "SAMPLER_OVER_LIMIT", NULL },
	{ 0x0000001c, "TEXTURE_OVER_LIMIT", NULL },
	{ 0x0000001e, "GP_TOO_MANY_OUTPUTS", NULL },
	{ 0x0000001f, "RT_BPP128_WITH_MS8", NULL },
	{ 0x00000021, "Z_OUT_OF_BOUNDS", NULL },
	{ 0x00000023, "XY_OUT_OF_BOUNDS", NULL },
	{ 0x00000024, "VP_ZERO_INPUTS", NULL },
	{ 0x00000027, "CP_MORE_PARAMS_THAN_SHARED", NULL },
	{ 0x00000028, "CP_NO_REG_SPACE_STRIPED", NULL },
	{ 0x00000029, "CP_NO_REG_SPACE_PACKED", NULL },
	{ 0x0000002a, "CP_NOT_ENOUGH_WARPS", NULL },
	{ 0x0000002b, "CP_BLOCK_SIZE_MISMATCH", NULL },
	{ 0x0000002c, "CP_NOT_ENOUGH_LOCAL_WARPS", NULL },
	{ 0x0000002d, "CP_NOT_ENOUGH_STACK_WARPS", NULL },
	{ 0x0000002e, "CP_NO_BLOCKDIM_LATCH", NULL },
	{ 0x00000031, "ENG2D_FORMAT_MISMATCH", NULL },
	{ 0x0000003f, "PRIMITIVE_ID_NEEDS_GP", NULL },
	{ 0x00000044, "SEMANTIC_VIEWPORT_OVER_LIMIT", NULL },
	{ 0x00000045, "SEMANTIC_COLOR_FRONT_OVER_LIMIT", NULL },
	{ 0x00000046, "LAYER_ID_NEEDS_GP", NULL },
	{ 0x00000047, "SEMANTIC_CLIP_OVER_LIMIT", NULL },
	{ 0x00000048, "SEMANTIC_PTSZ_OVER_LIMIT", NULL },
	{}
};

static struct nouveau_bitfield nv50_graph_intr[] = {
	{ 0x00000001, "NOTIFY" },
	{ 0x00000002, "COMPUTE_QUERY" },
	{ 0x00000010, "ILLEGAL_MTHD" },
	{ 0x00000020, "ILLEGAL_CLASS" },
	{ 0x00000040, "DOUBLE_NOTIFY" },
	{ 0x00001000, "CONTEXT_SWITCH" },
	{ 0x00010000, "BUFFER_NOTIFY" },
	{ 0x00100000, "DATA_ERROR" },
	{ 0x00200000, "TRAP" },
	{ 0x01000000, "SINGLE_STEP" },
	{}
};

static void
nv50_pgraph_mp_trap(struct nouveau_device *ndev, int tpid, int display)
{
	u32 units = nv_rd32(ndev, 0x1540);
	u32 addr, mp10, status, pc, oplow, ophigh;
	int i;
	int mps = 0;
	for (i = 0; i < 4; i++) {
		if (!(units & 1 << (i+24)))
			continue;
		if (ndev->chipset < 0xa0)
			addr = 0x408200 + (tpid << 12) + (i << 7);
		else
			addr = 0x408100 + (tpid << 11) + (i << 7);
		mp10 = nv_rd32(ndev, addr + 0x10);
		status = nv_rd32(ndev, addr + 0x14);
		if (!status)
			continue;
		if (display) {
			nv_rd32(ndev, addr + 0x20);
			pc = nv_rd32(ndev, addr + 0x24);
			oplow = nv_rd32(ndev, addr + 0x70);
			ophigh = nv_rd32(ndev, addr + 0x74);
			NV_INFO(ndev, "PGRAPH_TRAP_MP_EXEC - "
					"TP %d MP %d: ", tpid, i);
			nouveau_enum_print(nv50_mp_exec_error_names, status);
			printk(" at %06x warp %d, opcode %08x %08x\n",
					pc&0xffffff, pc >> 24,
					oplow, ophigh);
		}
		nv_wr32(ndev, addr + 0x10, mp10);
		nv_wr32(ndev, addr + 0x14, 0);
		mps++;
	}
	if (!mps && display)
		NV_INFO(ndev, "PGRAPH_TRAP_MP_EXEC - TP %d: "
				"No MPs claiming errors?\n", tpid);
}

static void
nv50_pgraph_tp_trap(struct nouveau_device *ndev, int type, u32 ustatus_old,
		u32 ustatus_new, int display, const char *name)
{
	int tps = 0;
	u32 units = nv_rd32(ndev, 0x1540);
	int i, r;
	u32 ustatus_addr, ustatus;
	for (i = 0; i < 16; i++) {
		if (!(units & (1 << i)))
			continue;
		if (ndev->chipset < 0xa0)
			ustatus_addr = ustatus_old + (i << 12);
		else
			ustatus_addr = ustatus_new + (i << 11);
		ustatus = nv_rd32(ndev, ustatus_addr) & 0x7fffffff;
		if (!ustatus)
			continue;
		tps++;
		switch (type) {
		case 6: /* texture error... unknown for now */
			if (display) {
				NV_ERROR(ndev, "magic set %d:\n", i);
				for (r = ustatus_addr + 4; r <= ustatus_addr + 0x10; r += 4)
					NV_ERROR(ndev, "\t0x%08x: 0x%08x\n", r,
						nv_rd32(ndev, r));
			}
			break;
		case 7: /* MP error */
			if (ustatus & 0x04030000) {
				nv50_pgraph_mp_trap(ndev, i, display);
				ustatus &= ~0x04030000;
			}
			break;
		case 8: /* TPDMA error */
			{
			u32 e0c = nv_rd32(ndev, ustatus_addr + 4);
			u32 e10 = nv_rd32(ndev, ustatus_addr + 8);
			u32 e14 = nv_rd32(ndev, ustatus_addr + 0xc);
			u32 e18 = nv_rd32(ndev, ustatus_addr + 0x10);
			u32 e1c = nv_rd32(ndev, ustatus_addr + 0x14);
			u32 e20 = nv_rd32(ndev, ustatus_addr + 0x18);
			u32 e24 = nv_rd32(ndev, ustatus_addr + 0x1c);
			/* 2d engine destination */
			if (ustatus & 0x00000010) {
				if (display) {
					NV_INFO(ndev, "PGRAPH_TRAP_TPDMA_2D - TP %d - Unknown fault at address %02x%08x\n",
							i, e14, e10);
					NV_INFO(ndev, "PGRAPH_TRAP_TPDMA_2D - TP %d - e0c: %08x, e18: %08x, e1c: %08x, e20: %08x, e24: %08x\n",
							i, e0c, e18, e1c, e20, e24);
				}
				ustatus &= ~0x00000010;
			}
			/* Render target */
			if (ustatus & 0x00000040) {
				if (display) {
					NV_INFO(ndev, "PGRAPH_TRAP_TPDMA_RT - TP %d - Unknown fault at address %02x%08x\n",
							i, e14, e10);
					NV_INFO(ndev, "PGRAPH_TRAP_TPDMA_RT - TP %d - e0c: %08x, e18: %08x, e1c: %08x, e20: %08x, e24: %08x\n",
							i, e0c, e18, e1c, e20, e24);
				}
				ustatus &= ~0x00000040;
			}
			/* CUDA memory: l[], g[] or stack. */
			if (ustatus & 0x00000080) {
				if (display) {
					if (e18 & 0x80000000) {
						/* g[] read fault? */
						NV_INFO(ndev, "PGRAPH_TRAP_TPDMA - TP %d - Global read fault at address %02x%08x\n",
								i, e14, e10 | ((e18 >> 24) & 0x1f));
						e18 &= ~0x1f000000;
					} else if (e18 & 0xc) {
						/* g[] write fault? */
						NV_INFO(ndev, "PGRAPH_TRAP_TPDMA - TP %d - Global write fault at address %02x%08x\n",
								i, e14, e10 | ((e18 >> 7) & 0x1f));
						e18 &= ~0x00000f80;
					} else {
						NV_INFO(ndev, "PGRAPH_TRAP_TPDMA - TP %d - Unknown CUDA fault at address %02x%08x\n",
								i, e14, e10);
					}
					NV_INFO(ndev, "PGRAPH_TRAP_TPDMA - TP %d - e0c: %08x, e18: %08x, e1c: %08x, e20: %08x, e24: %08x\n",
							i, e0c, e18, e1c, e20, e24);
				}
				ustatus &= ~0x00000080;
			}
			}
			break;
		}
		if (ustatus) {
			if (display)
				NV_INFO(ndev, "%s - TP%d: Unhandled ustatus 0x%08x\n", name, i, ustatus);
		}
		nv_wr32(ndev, ustatus_addr, 0xc0000000);
	}

	if (!tps && display)
		NV_INFO(ndev, "%s - No TPs claiming errors?\n", name);
}

static int
nv50_pgraph_trap_handler(struct nouveau_device *ndev, u32 display, u64 inst, u32 chid)
{
	u32 status = nv_rd32(ndev, 0x400108);
	u32 ustatus;

	if (!status && display) {
		NV_INFO(ndev, "PGRAPH - TRAP: no units reporting traps?\n");
		return 1;
	}

	/* DISPATCH: Relays commands to other units and handles NOTIFY,
	 * COND, QUERY. If you get a trap from it, the command is still stuck
	 * in DISPATCH and you need to do something about it. */
	if (status & 0x001) {
		ustatus = nv_rd32(ndev, 0x400804) & 0x7fffffff;
		if (!ustatus && display) {
			NV_INFO(ndev, "PGRAPH_TRAP_DISPATCH - no ustatus?\n");
		}

		nv_wr32(ndev, 0x400500, 0x00000000);

		/* Known to be triggered by screwed up NOTIFY and COND... */
		if (ustatus & 0x00000001) {
			u32 addr = nv_rd32(ndev, 0x400808);
			u32 subc = (addr & 0x00070000) >> 16;
			u32 mthd = (addr & 0x00001ffc);
			u32 datal = nv_rd32(ndev, 0x40080c);
			u32 datah = nv_rd32(ndev, 0x400810);
			u32 class = nv_rd32(ndev, 0x400814);
			u32 r848 = nv_rd32(ndev, 0x400848);

			NV_INFO(ndev, "PGRAPH - TRAP DISPATCH_FAULT\n");
			if (display && (addr & 0x80000000)) {
				NV_INFO(ndev, "PGRAPH - ch %d (0x%010llx) "
					     "subc %d class 0x%04x mthd 0x%04x "
					     "data 0x%08x%08x "
					     "400808 0x%08x 400848 0x%08x\n",
					chid, inst, subc, class, mthd, datah,
					datal, addr, r848);
			} else
			if (display) {
				NV_INFO(ndev, "PGRAPH - no stuck command?\n");
			}

			nv_wr32(ndev, 0x400808, 0);
			nv_wr32(ndev, 0x4008e8, nv_rd32(ndev, 0x4008e8) & 3);
			nv_wr32(ndev, 0x400848, 0);
			ustatus &= ~0x00000001;
		}

		if (ustatus & 0x00000002) {
			u32 addr = nv_rd32(ndev, 0x40084c);
			u32 subc = (addr & 0x00070000) >> 16;
			u32 mthd = (addr & 0x00001ffc);
			u32 data = nv_rd32(ndev, 0x40085c);
			u32 class = nv_rd32(ndev, 0x400814);

			NV_INFO(ndev, "PGRAPH - TRAP DISPATCH_QUERY\n");
			if (display && (addr & 0x80000000)) {
				NV_INFO(ndev, "PGRAPH - ch %d (0x%010llx) "
					     "subc %d class 0x%04x mthd 0x%04x "
					     "data 0x%08x 40084c 0x%08x\n",
					chid, inst, subc, class, mthd,
					data, addr);
			} else
			if (display) {
				NV_INFO(ndev, "PGRAPH - no stuck command?\n");
			}

			nv_wr32(ndev, 0x40084c, 0);
			ustatus &= ~0x00000002;
		}

		if (ustatus && display) {
			NV_INFO(ndev, "PGRAPH - TRAP_DISPATCH (unknown "
				      "0x%08x)\n", ustatus);
		}

		nv_wr32(ndev, 0x400804, 0xc0000000);
		nv_wr32(ndev, 0x400108, 0x001);
		status &= ~0x001;
		if (!status)
			return 0;
	}

	/* M2MF: Memory to memory copy engine. */
	if (status & 0x002) {
		u32 ustatus = nv_rd32(ndev, 0x406800) & 0x7fffffff;
		if (display) {
			NV_INFO(ndev, "PGRAPH - TRAP_M2MF");
			nouveau_bitfield_print(nv50_graph_trap_m2mf, ustatus);
			printk("\n");
			NV_INFO(ndev, "PGRAPH - TRAP_M2MF %08x %08x %08x %08x\n",
				nv_rd32(ndev, 0x406804), nv_rd32(ndev, 0x406808),
				nv_rd32(ndev, 0x40680c), nv_rd32(ndev, 0x406810));

		}

		/* No sane way found yet -- just reset the bugger. */
		nv_wr32(ndev, 0x400040, 2);
		nv_wr32(ndev, 0x400040, 0);
		nv_wr32(ndev, 0x406800, 0xc0000000);
		nv_wr32(ndev, 0x400108, 0x002);
		status &= ~0x002;
	}

	/* VFETCH: Fetches data from vertex buffers. */
	if (status & 0x004) {
		u32 ustatus = nv_rd32(ndev, 0x400c04) & 0x7fffffff;
		if (display) {
			NV_INFO(ndev, "PGRAPH - TRAP_VFETCH");
			nouveau_bitfield_print(nv50_graph_trap_vfetch, ustatus);
			printk("\n");
			NV_INFO(ndev, "PGRAPH - TRAP_VFETCH %08x %08x %08x %08x\n",
				nv_rd32(ndev, 0x400c00), nv_rd32(ndev, 0x400c08),
				nv_rd32(ndev, 0x400c0c), nv_rd32(ndev, 0x400c10));
		}

		nv_wr32(ndev, 0x400c04, 0xc0000000);
		nv_wr32(ndev, 0x400108, 0x004);
		status &= ~0x004;
	}

	/* STRMOUT: DirectX streamout / OpenGL transform feedback. */
	if (status & 0x008) {
		ustatus = nv_rd32(ndev, 0x401800) & 0x7fffffff;
		if (display) {
			NV_INFO(ndev, "PGRAPH - TRAP_STRMOUT");
			nouveau_bitfield_print(nv50_graph_trap_strmout, ustatus);
			printk("\n");
			NV_INFO(ndev, "PGRAPH - TRAP_STRMOUT %08x %08x %08x %08x\n",
				nv_rd32(ndev, 0x401804), nv_rd32(ndev, 0x401808),
				nv_rd32(ndev, 0x40180c), nv_rd32(ndev, 0x401810));

		}

		/* No sane way found yet -- just reset the bugger. */
		nv_wr32(ndev, 0x400040, 0x80);
		nv_wr32(ndev, 0x400040, 0);
		nv_wr32(ndev, 0x401800, 0xc0000000);
		nv_wr32(ndev, 0x400108, 0x008);
		status &= ~0x008;
	}

	/* CCACHE: Handles code and c[] caches and fills them. */
	if (status & 0x010) {
		ustatus = nv_rd32(ndev, 0x405018) & 0x7fffffff;
		if (display) {
			NV_INFO(ndev, "PGRAPH - TRAP_CCACHE");
			nouveau_bitfield_print(nv50_graph_trap_ccache, ustatus);
			printk("\n");
			NV_INFO(ndev, "PGRAPH - TRAP_CCACHE %08x %08x %08x %08x"
				     " %08x %08x %08x\n",
				nv_rd32(ndev, 0x405000), nv_rd32(ndev, 0x405004),
				nv_rd32(ndev, 0x405008), nv_rd32(ndev, 0x40500c),
				nv_rd32(ndev, 0x405010), nv_rd32(ndev, 0x405014),
				nv_rd32(ndev, 0x40501c));

		}

		nv_wr32(ndev, 0x405018, 0xc0000000);
		nv_wr32(ndev, 0x400108, 0x010);
		status &= ~0x010;
	}

	/* Unknown, not seen yet... 0x402000 is the only trap status reg
	 * remaining, so try to handle it anyway. Perhaps related to that
	 * unknown DMA slot on tesla? */
	if (status & 0x20) {
		ustatus = nv_rd32(ndev, 0x402000) & 0x7fffffff;
		if (display)
			NV_INFO(ndev, "PGRAPH - TRAP_UNKC04 0x%08x\n", ustatus);
		nv_wr32(ndev, 0x402000, 0xc0000000);
		/* no status modifiction on purpose */
	}

	/* TEXTURE: CUDA texturing units */
	if (status & 0x040) {
		nv50_pgraph_tp_trap(ndev, 6, 0x408900, 0x408600, display,
				    "PGRAPH - TRAP_TEXTURE");
		nv_wr32(ndev, 0x400108, 0x040);
		status &= ~0x040;
	}

	/* MP: CUDA execution engines. */
	if (status & 0x080) {
		nv50_pgraph_tp_trap(ndev, 7, 0x408314, 0x40831c, display,
				    "PGRAPH - TRAP_MP");
		nv_wr32(ndev, 0x400108, 0x080);
		status &= ~0x080;
	}

	/* TPDMA:  Handles TP-initiated uncached memory accesses:
	 * l[], g[], stack, 2d surfaces, render targets. */
	if (status & 0x100) {
		nv50_pgraph_tp_trap(ndev, 8, 0x408e08, 0x408708, display,
				    "PGRAPH - TRAP_TPDMA");
		nv_wr32(ndev, 0x400108, 0x100);
		status &= ~0x100;
	}

	if (status) {
		if (display)
			NV_INFO(ndev, "PGRAPH - TRAP: unknown 0x%08x\n", status);
		nv_wr32(ndev, 0x400108, status);
	}

	return 1;
}

int
nv50_graph_isr_chid(struct nouveau_device *ndev, u64 inst)
{
	struct nouveau_fifo_priv *pfifo = nv_engine(ndev, NVOBJ_ENGINE_FIFO);
	struct nouveau_channel *chan;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&ndev->channels.lock, flags);
	for (i = 0; i < pfifo->channels; i++) {
		chan = ndev->channels.ptr[i];
		if (!chan || !chan->ramin)
			continue;

		if (inst == chan->ramin->vinst)
			break;
	}
	spin_unlock_irqrestore(&ndev->channels.lock, flags);
	return i;
}

static void
nv50_graph_isr(struct nouveau_device *ndev)
{
	u32 stat;

	while ((stat = nv_rd32(ndev, 0x400100))) {
		u64 inst = (u64)(nv_rd32(ndev, 0x40032c) & 0x0fffffff) << 12;
		u32 chid = nv50_graph_isr_chid(ndev, inst);
		u32 addr = nv_rd32(ndev, NV04_PGRAPH_TRAPPED_ADDR);
		u32 subc = (addr & 0x00070000) >> 16;
		u32 mthd = (addr & 0x00001ffc);
		u32 data = nv_rd32(ndev, NV04_PGRAPH_TRAPPED_DATA);
		u32 class = nv_rd32(ndev, 0x400814);
		u32 show = stat;

		if (stat & 0x00000010) {
			if (!nouveau_gpuobj_mthd_call2(ndev, chid, class,
						       mthd, data))
				show &= ~0x00000010;
		}

		show = (show && nouveau_ratelimit()) ? show : 0;

		if (show & 0x00100000) {
			u32 ecode = nv_rd32(ndev, 0x400110);
			NV_INFO(ndev, "PGRAPH - DATA_ERROR ");
			nouveau_enum_print(nv50_data_error_names, ecode);
			printk("\n");
		}

		if (stat & 0x00200000) {
			if (!nv50_pgraph_trap_handler(ndev, show, inst, chid))
				show &= ~0x00200000;
		}

		nv_wr32(ndev, 0x400100, stat);
		nv_wr32(ndev, 0x400500, 0x00010001);

		if (show) {
			NV_INFO(ndev, "PGRAPH -");
			nouveau_bitfield_print(nv50_graph_intr, show);
			printk("\n");
			NV_INFO(ndev, "PGRAPH - ch %d (0x%010llx) subc %d "
				     "class 0x%04x mthd 0x%04x data 0x%08x\n",
				chid, inst, subc, class, mthd, data);
			nv50_fb_vm_trap(ndev, 1);
		}
	}

	if (nv_rd32(ndev, 0x400824) & (1 << 31))
		nv_wr32(ndev, 0x400824, nv_rd32(ndev, 0x400824) & ~(1 << 31));
}

static void
nv50_graph_destroy(struct nouveau_device *ndev, int engine)
{
	struct nv50_graph_engine *pgraph = nv_engine(ndev, engine);

	NVOBJ_ENGINE_DEL(ndev, GR);

	nouveau_irq_unregister(ndev, 12);
	kfree(pgraph);
}

int
nv50_graph_create(struct nouveau_device *ndev)
{
	struct nv50_graph_engine *pgraph;
	int ret;

	pgraph = kzalloc(sizeof(*pgraph),GFP_KERNEL);
	if (!pgraph)
		return -ENOMEM;

	ret = nv50_grctx_init(ndev, pgraph->ctxprog, ARRAY_SIZE(pgraph->ctxprog),
				  &pgraph->ctxprog_size,
				  &pgraph->grctx_size);
	if (ret) {
		NV_ERROR(ndev, "PGRAPH: ctxprog build failed\n");
		kfree(pgraph);
		return 0;
	}

	pgraph->base.destroy = nv50_graph_destroy;
	pgraph->base.init = nv50_graph_init;
	pgraph->base.fini = nv50_graph_fini;
	pgraph->base.context_new = nv50_graph_context_new;
	pgraph->base.context_del = nv50_graph_context_del;
	pgraph->base.object_new = nv50_graph_object_new;
	if (ndev->chipset == 0x50 || ndev->chipset == 0xac)
		pgraph->base.tlb_flush = nv50_graph_tlb_flush;
	else
		pgraph->base.tlb_flush = nv84_graph_tlb_flush;

	nouveau_irq_register(ndev, 12, nv50_graph_isr);

	NVOBJ_ENGINE_ADD(ndev, GR, &pgraph->base);
	NVOBJ_CLASS(ndev, 0x0030, GR); /* null */
	NVOBJ_CLASS(ndev, 0x5039, GR); /* m2mf */
	NVOBJ_CLASS(ndev, 0x502d, GR); /* 2d */

	/* tesla */
	if (ndev->chipset == 0x50)
		NVOBJ_CLASS(ndev, 0x5097, GR); /* tesla (nv50) */
	else
	if (ndev->chipset < 0xa0)
		NVOBJ_CLASS(ndev, 0x8297, GR); /* tesla (nv8x/nv9x) */
	else {
		switch (ndev->chipset) {
		case 0xa0:
		case 0xaa:
		case 0xac:
			NVOBJ_CLASS(ndev, 0x8397, GR);
			break;
		case 0xa3:
		case 0xa5:
		case 0xa8:
			NVOBJ_CLASS(ndev, 0x8597, GR);
			break;
		case 0xaf:
			NVOBJ_CLASS(ndev, 0x8697, GR);
			break;
		}
	}

	/* compute */
	NVOBJ_CLASS(ndev, 0x50c0, GR);
	if (ndev->chipset  > 0xa0 &&
	    ndev->chipset != 0xaa &&
	    ndev->chipset != 0xac)
		NVOBJ_CLASS(ndev, 0x85c0, GR);

	return 0;
}
