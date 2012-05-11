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
#include "nouveau_bios.h"
#include "nouveau_pm.h"
#include "nouveau_clock.h"

struct nvc0_clock_priv {
	struct nouveau_clock base;
};

static u32 read_div(struct nouveau_device *, int, u32, u32);
static u32 read_pll(struct nouveau_device *, u32);

static u32
read_vco(struct nouveau_device *ndev, u32 dsrc)
{
	u32 ssrc = nv_rd32(ndev, dsrc);
	if (!(ssrc & 0x00000100))
		return read_pll(ndev, 0x00e800);
	return read_pll(ndev, 0x00e820);
}

static u32
read_pll(struct nouveau_device *ndev, u32 pll)
{
	u32 ctrl = nv_rd32(ndev, pll + 0);
	u32 coef = nv_rd32(ndev, pll + 4);
	u32 P = (coef & 0x003f0000) >> 16;
	u32 N = (coef & 0x0000ff00) >> 8;
	u32 M = (coef & 0x000000ff) >> 0;
	u32 sclk, doff;

	if (!(ctrl & 0x00000001))
		return 0;

	switch (pll & 0xfff000) {
	case 0x00e000:
		sclk = 27000;
		P = 1;
		break;
	case 0x137000:
		doff = (pll - 0x137000) / 0x20;
		sclk = read_div(ndev, doff, 0x137120, 0x137140);
		break;
	case 0x132000:
		switch (pll) {
		case 0x132000:
			sclk = read_pll(ndev, 0x132020);
			break;
		case 0x132020:
			sclk = read_div(ndev, 0, 0x137320, 0x137330);
			break;
		default:
			return 0;
		}
		break;
	default:
		return 0;
	}

	return sclk * N / M / P;
}

static u32
read_div(struct nouveau_device *ndev, int doff, u32 dsrc, u32 dctl)
{
	u32 ssrc = nv_rd32(ndev, dsrc + (doff * 4));
	u32 sctl = nv_rd32(ndev, dctl + (doff * 4));

	switch (ssrc & 0x00000003) {
	case 0:
		if ((ssrc & 0x00030000) != 0x00030000)
			return 27000;
		return 108000;
	case 2:
		return 100000;
	case 3:
		if (sctl & 0x80000000) {
			u32 sclk = read_vco(ndev, dsrc + (doff * 4));
			u32 sdiv = (sctl & 0x0000003f) + 2;
			return (sclk * 2) / sdiv;
		}

		return read_vco(ndev, dsrc + (doff * 4));
	default:
		return 0;
	}
}

static u32
read_mem(struct nouveau_device *ndev)
{
	u32 ssel = nv_rd32(ndev, 0x1373f0);
	if (ssel & 0x00000001)
		return read_div(ndev, 0, 0x137300, 0x137310);
	return read_pll(ndev, 0x132000);
}

static u32
read_clk(struct nouveau_device *ndev, int clk)
{
	u32 sctl = nv_rd32(ndev, 0x137250 + (clk * 4));
	u32 ssel = nv_rd32(ndev, 0x137100);
	u32 sclk, sdiv;

	if (ssel & (1 << clk)) {
		if (clk < 7)
			sclk = read_pll(ndev, 0x137000 + (clk * 0x20));
		else
			sclk = read_pll(ndev, 0x1370e0);
		sdiv = ((sctl & 0x00003f00) >> 8) + 2;
	} else {
		sclk = read_div(ndev, clk, 0x137160, 0x1371d0);
		sdiv = ((sctl & 0x0000003f) >> 0) + 2;
	}

	if (sctl & 0x80000000)
		return (sclk * 2) / sdiv;
	return sclk;
}

static int
nvc0_clocks_get(struct nouveau_clock *pclk, struct nouveau_pm_level *perflvl)
{
	struct nouveau_device *ndev = pclk->base.device;

	perflvl->shader = read_clk(ndev, 0x00);
	perflvl->core   = perflvl->shader / 2;
	perflvl->memory = read_mem(ndev);
	perflvl->rop    = read_clk(ndev, 0x01);
	perflvl->hub07  = read_clk(ndev, 0x02);
	perflvl->hub06  = read_clk(ndev, 0x07);
	perflvl->hub01  = read_clk(ndev, 0x08);
	perflvl->copy   = read_clk(ndev, 0x09);
	perflvl->daemon = read_clk(ndev, 0x0c);
	perflvl->vdec   = read_clk(ndev, 0x0e);
	return 0;
}

struct nvc0_pm_clock {
	u32 freq;
	u32 ssel;
	u32 mdiv;
	u32 dsrc;
	u32 ddiv;
	u32 coef;
};

struct nvc0_pm_state {
	struct nouveau_pm_level *perflvl;
	struct nvc0_pm_clock eng[16];
	struct nvc0_pm_clock mem;
};

static u32
calc_div(struct nouveau_device *ndev, int clk, u32 ref, u32 freq, u32 *ddiv)
{
	u32 div = min((ref * 2) / freq, (u32)65);
	if (div < 2)
		div = 2;

	*ddiv = div - 2;
	return (ref * 2) / div;
}

static u32
calc_src(struct nouveau_device *ndev, int clk, u32 freq, u32 *dsrc, u32 *ddiv)
{
	u32 sclk;

	/* use one of the fixed frequencies if possible */
	*ddiv = 0x00000000;
	switch (freq) {
	case  27000:
	case 108000:
		*dsrc = 0x00000000;
		if (freq == 108000)
			*dsrc |= 0x00030000;
		return freq;
	case 100000:
		*dsrc = 0x00000002;
		return freq;
	default:
		*dsrc = 0x00000003;
		break;
	}

	/* otherwise, calculate the closest divider */
	sclk = read_vco(ndev, clk);
	if (clk < 7)
		sclk = calc_div(ndev, clk, sclk, freq, ddiv);
	return sclk;
}

static u32
calc_pll(struct nouveau_device *ndev, int clk, u32 freq, u32 *coef)
{
	struct pll_lims limits;
	int N, M, P, ret;

	ret = get_pll_limits(ndev, 0x137000 + (clk * 0x20), &limits);
	if (ret)
		return 0;

	limits.refclk = read_div(ndev, clk, 0x137120, 0x137140);
	if (!limits.refclk)
		return 0;

	ret = nva3_calc_pll(ndev, &limits, freq, &N, NULL, &M, &P);
	if (ret <= 0)
		return 0;

	*coef = (P << 16) | (N << 8) | M;
	return ret;
}

/* A (likely rather simplified and incomplete) view of the clock tree
 *
 * Key:
 *
 * S: source select
 * D: divider
 * P: pll
 * F: switch
 *
 * Engine clocks:
 *
 * 137250(D) ---- 137100(F0) ---- 137160(S)/1371d0(D) ------------------- ref
 *                      (F1) ---- 1370X0(P) ---- 137120(S)/137140(D) ---- ref
 *
 * Not all registers exist for all clocks.  For example: clocks >= 8 don't
 * have their own PLL (all tied to clock 7's PLL when in PLL mode), nor do
 * they have the divider at 1371d0, though the source selection at 137160
 * still exists.  You must use the divider at 137250 for these instead.
 *
 * Memory clock:
 *
 * TBD, read_mem() above is likely very wrong...
 *
 */

static int
calc_clk(struct nouveau_device *ndev, int clk, struct nvc0_pm_clock *info, u32 freq)
{
	u32 src0, div0, div1D, div1P = 0;
	u32 clk0, clk1 = 0;

	/* invalid clock domain */
	if (!freq)
		return 0;

	/* first possible path, using only dividers */
	clk0 = calc_src(ndev, clk, freq, &src0, &div0);
	clk0 = calc_div(ndev, clk, clk0, freq, &div1D);

	/* see if we can get any closer using PLLs */
	if (clk0 != freq && (0x00004387 & (1 << clk))) {
		if (clk < 7)
			clk1 = calc_pll(ndev, clk, freq, &info->coef);
		else
			clk1 = read_pll(ndev, 0x1370e0);
		clk1 = calc_div(ndev, clk, clk1, freq, &div1P);
	}

	/* select the method which gets closest to target freq */
	if (abs((int)freq - clk0) <= abs((int)freq - clk1)) {
		info->dsrc = src0;
		if (div0) {
			info->ddiv |= 0x80000000;
			info->ddiv |= div0 << 8;
			info->ddiv |= div0;
		}
		if (div1D) {
			info->mdiv |= 0x80000000;
			info->mdiv |= div1D;
		}
		info->ssel = 0;
		info->freq = clk0;
	} else {
		if (div1P) {
			info->mdiv |= 0x80000000;
			info->mdiv |= div1P << 8;
		}
		info->ssel = (1 << clk);
		info->freq = clk1;
	}

	return 0;
}

static int
calc_mem(struct nouveau_device *ndev, struct nvc0_pm_clock *info, u32 freq)
{
	struct pll_lims pll;
	int N, M, P, ret;
	u32 ctrl;

	/* mclk pll input freq comes from another pll, make sure it's on */
	ctrl = nv_rd32(ndev, 0x132020);
	if (!(ctrl & 0x00000001)) {
		/* if not, program it to 567MHz.  nfi where this value comes
		 * from - it looks like it's in the pll limits table for
		 * 132000 but the binary driver ignores all my attempts to
		 * change this value.
		 */
		nv_wr32(ndev, 0x137320, 0x00000103);
		nv_wr32(ndev, 0x137330, 0x81200606);
		nv_wait(ndev, 0x132020, 0x00010000, 0x00010000);
		nv_wr32(ndev, 0x132024, 0x0001150f);
		nv_mask(ndev, 0x132020, 0x00000001, 0x00000001);
		nv_wait(ndev, 0x137390, 0x00020000, 0x00020000);
		nv_mask(ndev, 0x132020, 0x00000004, 0x00000004);
	}

	/* for the moment, until the clock tree is better understood, use
	 * pll mode for all clock frequencies
	 */
	ret = get_pll_limits(ndev, 0x132000, &pll);
	if (ret == 0) {
		pll.refclk = read_pll(ndev, 0x132020);
		if (pll.refclk) {
			ret = nva3_calc_pll(ndev, &pll, freq, &N, NULL, &M, &P);
			if (ret > 0) {
				info->coef = (P << 16) | (N << 8) | M;
				return 0;
			}
		}
	}

	return -EINVAL;
}

static void *
nvc0_clocks_pre(struct nouveau_clock *pclk, struct nouveau_pm_level *perflvl)
{
	struct nouveau_device *ndev = pclk->base.device;
	struct nvc0_pm_state *info;
	int ret;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	/* NFI why this is still in the performance table, the ROPCs appear
	 * to get their clock from clock 2 ("hub07", actually hub05 on this
	 * chip, but, anyway...) as well.  nvatiming confirms hub05 and ROP
	 * are always the same freq with the binary driver even when the
	 * performance table says they should differ.
	 */
	if (ndev->chipset == 0xd9)
		perflvl->rop = 0;

	if ((ret = calc_clk(ndev, 0x00, &info->eng[0x00], perflvl->shader)) ||
	    (ret = calc_clk(ndev, 0x01, &info->eng[0x01], perflvl->rop)) ||
	    (ret = calc_clk(ndev, 0x02, &info->eng[0x02], perflvl->hub07)) ||
	    (ret = calc_clk(ndev, 0x07, &info->eng[0x07], perflvl->hub06)) ||
	    (ret = calc_clk(ndev, 0x08, &info->eng[0x08], perflvl->hub01)) ||
	    (ret = calc_clk(ndev, 0x09, &info->eng[0x09], perflvl->copy)) ||
	    (ret = calc_clk(ndev, 0x0c, &info->eng[0x0c], perflvl->daemon)) ||
	    (ret = calc_clk(ndev, 0x0e, &info->eng[0x0e], perflvl->vdec))) {
		kfree(info);
		return ERR_PTR(ret);
	}

	if (perflvl->memory) {
		ret = calc_mem(ndev, &info->mem, perflvl->memory);
		if (ret) {
			kfree(info);
			return ERR_PTR(ret);
		}
	}

	info->perflvl = perflvl;
	return info;
}

static void
prog_clk(struct nouveau_device *ndev, int clk, struct nvc0_pm_clock *info)
{
	/* program dividers at 137160/1371d0 first */
	if (clk < 7 && !info->ssel) {
		nv_mask(ndev, 0x1371d0 + (clk * 0x04), 0x80003f3f, info->ddiv);
		nv_wr32(ndev, 0x137160 + (clk * 0x04), info->dsrc);
	}

	/* switch clock to non-pll mode */
	nv_mask(ndev, 0x137100, (1 << clk), 0x00000000);
	nv_wait(ndev, 0x137100, (1 << clk), 0x00000000);

	/* reprogram pll */
	if (clk < 7) {
		/* make sure it's disabled first... */
		u32 base = 0x137000 + (clk * 0x20);
		u32 ctrl = nv_rd32(ndev, base + 0x00);
		if (ctrl & 0x00000001) {
			nv_mask(ndev, base + 0x00, 0x00000004, 0x00000000);
			nv_mask(ndev, base + 0x00, 0x00000001, 0x00000000);
		}
		/* program it to new values, if necessary */
		if (info->ssel) {
			nv_wr32(ndev, base + 0x04, info->coef);
			nv_mask(ndev, base + 0x00, 0x00000001, 0x00000001);
			nv_wait(ndev, base + 0x00, 0x00020000, 0x00020000);
			nv_mask(ndev, base + 0x00, 0x00020004, 0x00000004);
		}
	}

	/* select pll/non-pll mode, and program final clock divider */
	nv_mask(ndev, 0x137100, (1 << clk), info->ssel);
	nv_wait(ndev, 0x137100, (1 << clk), info->ssel);
	nv_mask(ndev, 0x137250 + (clk * 0x04), 0x00003f3f, info->mdiv);
}

static void
mclk_precharge(struct nouveau_mem_exec_func *exec)
{
}

static void
mclk_refresh(struct nouveau_mem_exec_func *exec)
{
}

static void
mclk_refresh_auto(struct nouveau_mem_exec_func *exec, bool enable)
{
	nv_wr32(exec->device, 0x10f210, enable ? 0x80000000 : 0x00000000);
}

static void
mclk_refresh_self(struct nouveau_mem_exec_func *exec, bool enable)
{
}

static void
mclk_wait(struct nouveau_mem_exec_func *exec, u32 nsec)
{
	udelay((nsec + 500) / 1000);
}

static u32
mclk_mrg(struct nouveau_mem_exec_func *exec, int mr)
{
	struct nouveau_device *ndev = exec->device;
	if (ndev->vram_type != NV_MEM_TYPE_GDDR5) {
		if (mr <= 1)
			return nv_rd32(ndev, 0x10f300 + ((mr - 0) * 4));
		return nv_rd32(ndev, 0x10f320 + ((mr - 2) * 4));
	} else {
		if (mr == 0)
			return nv_rd32(ndev, 0x10f300 + (mr * 4));
		else
		if (mr <= 7)
			return nv_rd32(ndev, 0x10f32c + (mr * 4));
		return nv_rd32(ndev, 0x10f34c);
	}
}

static void
mclk_mrs(struct nouveau_mem_exec_func *exec, int mr, u32 data)
{
	struct nouveau_device *ndev = exec->device;
	if (ndev->vram_type != NV_MEM_TYPE_GDDR5) {
		if (mr <= 1) {
			nv_wr32(ndev, 0x10f300 + ((mr - 0) * 4), data);
			if (ndev->vram_rank_B)
				nv_wr32(ndev, 0x10f308 + ((mr - 0) * 4), data);
		} else
		if (mr <= 3) {
			nv_wr32(ndev, 0x10f320 + ((mr - 2) * 4), data);
			if (ndev->vram_rank_B)
				nv_wr32(ndev, 0x10f328 + ((mr - 2) * 4), data);
		}
	} else {
		if      (mr ==  0) nv_wr32(ndev, 0x10f300 + (mr * 4), data);
		else if (mr <=  7) nv_wr32(ndev, 0x10f32c + (mr * 4), data);
		else if (mr == 15) nv_wr32(ndev, 0x10f34c, data);
	}
}

static void
mclk_clock_set(struct nouveau_mem_exec_func *exec)
{
	struct nvc0_pm_state *info = exec->priv;
	struct nouveau_device *ndev = exec->device;
	u32 ctrl = nv_rd32(ndev, 0x132000);

	nv_wr32(ndev, 0x137360, 0x00000001);
	nv_wr32(ndev, 0x137370, 0x00000000);
	nv_wr32(ndev, 0x137380, 0x00000000);
	if (ctrl & 0x00000001)
		nv_wr32(ndev, 0x132000, (ctrl &= ~0x00000001));

	nv_wr32(ndev, 0x132004, info->mem.coef);
	nv_wr32(ndev, 0x132000, (ctrl |= 0x00000001));
	nv_wait(ndev, 0x137390, 0x00000002, 0x00000002);
	nv_wr32(ndev, 0x132018, 0x00005000);

	nv_wr32(ndev, 0x137370, 0x00000001);
	nv_wr32(ndev, 0x137380, 0x00000001);
	nv_wr32(ndev, 0x137360, 0x00000000);
}

static void
mclk_timing_set(struct nouveau_mem_exec_func *exec)
{
	struct nvc0_pm_state *info = exec->priv;
	struct nouveau_pm_level *perflvl = info->perflvl;
	int i;

	for (i = 0; i < 5; i++)
		nv_wr32(exec->device, 0x10f290 + (i * 4), perflvl->timing.reg[i]);
}

static void
prog_mem(struct nouveau_device *ndev, struct nvc0_pm_state *info)
{
	struct nouveau_mem_exec_func exec = {
		.device = ndev,
		.precharge = mclk_precharge,
		.refresh = mclk_refresh,
		.refresh_auto = mclk_refresh_auto,
		.refresh_self = mclk_refresh_self,
		.wait = mclk_wait,
		.mrg = mclk_mrg,
		.mrs = mclk_mrs,
		.clock_set = mclk_clock_set,
		.timing_set = mclk_timing_set,
		.priv = info
	};

	if (ndev->chipset < 0xd0)
		nv_wr32(ndev, 0x611200, 0x00003300);
	else
		nv_wr32(ndev, 0x62c000, 0x03030000);

	nouveau_mem_exec(&exec, info->perflvl);

	if (ndev->chipset < 0xd0)
		nv_wr32(ndev, 0x611200, 0x00003300);
	else
		nv_wr32(ndev, 0x62c000, 0x03030300);
}

static int
nvc0_clocks_set(struct nouveau_clock *pclk, void *data)
{
	struct nouveau_device *ndev = pclk->base.device;
	struct nvc0_pm_state *info = data;
	int i;

	if (info->mem.coef)
		prog_mem(ndev, info);

	for (i = 0; i < 16; i++) {
		if (!info->eng[i].freq)
			continue;
		prog_clk(ndev, i, &info->eng[i]);
	}

	kfree(info);
	return 0;
}

int
nvc0_clock_create(struct nouveau_device *ndev, int subdev)
{
	struct nvc0_clock_priv *priv;
	int ret;

	ret = nouveau_subdev_create(ndev, subdev, "CLK", "clocks", &priv);
	if (ret)
		return ret;

	priv->base.perf_get = nvc0_clocks_get;
	priv->base.perf_pre = nvc0_clocks_pre;
	priv->base.perf_set = nvc0_clocks_set;
	return nouveau_subdev_init(ndev, subdev, ret);
}
