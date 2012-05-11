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
#include "nouveau_hw.h"
#include "nouveau_pm.h"
#include "nouveau_clock.h"

struct nv04_clock_priv {
	struct nouveau_clock base;
};

static int
nv04_clocks_get(struct nouveau_clock *pclk, struct nouveau_pm_level *perflvl)
{
	struct nouveau_device *ndev = pclk->base.device;
	int ret;

	ret = nouveau_hw_get_clock(ndev, PLL_CORE);
	if (ret < 0)
		return ret;
	perflvl->core = ret;

	ret = nouveau_hw_get_clock(ndev, PLL_MEMORY);
	if (ret < 0)
		return ret;
	perflvl->memory = ret;

	return 0;
}

struct nv04_pm_clock {
	struct pll_lims pll;
	struct nouveau_pll_vals calc;
};

struct nv04_pm_state {
	struct nv04_pm_clock core;
	struct nv04_pm_clock memory;
};

static int
calc_pll(struct nouveau_device *ndev, u32 id, int khz, struct nv04_pm_clock *clk)
{
	int ret;

	ret = get_pll_limits(ndev, id, &clk->pll);
	if (ret)
		return ret;

	ret = nouveau_calc_pll_mnp(ndev, &clk->pll, khz, &clk->calc);
	if (!ret)
		return -EINVAL;

	return 0;
}

static void *
nv04_clocks_pre(struct nouveau_clock *pclk, struct nouveau_pm_level *perflvl)
{
	struct nouveau_device *ndev = pclk->base.device;
	struct nv04_pm_state *info;
	int ret;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	ret = calc_pll(ndev, PLL_CORE, perflvl->core, &info->core);
	if (ret)
		goto error;

	if (perflvl->memory) {
		ret = calc_pll(ndev, PLL_MEMORY, perflvl->memory, &info->memory);
		if (ret)
			goto error;
	}

	return info;
error:
	kfree(info);
	return ERR_PTR(ret);
}

static void
prog_pll(struct nouveau_device *ndev, struct nv04_pm_clock *clk)
{
	u32 reg = clk->pll.reg;

	/* thank the insane nouveau_hw_setpll() interface for this */
	if (ndev->card_type >= NV_40)
		reg += 4;

	nouveau_hw_setpll(ndev, reg, &clk->calc);
}

static int
nv04_clocks_set(struct nouveau_clock *pclk, void *pre_state)
{
	struct nouveau_device *ndev = pclk->base.device;
	struct nouveau_subdev *ptimer = nv_subdev(ndev, NVDEV_SUBDEV_TIMER);
	struct nv04_pm_state *state = pre_state;

	prog_pll(ndev, &state->core);

	if (state->memory.pll.reg) {
		prog_pll(ndev, &state->memory);
		if (ndev->card_type < NV_30) {
			if (ndev->card_type == NV_20)
				nv_mask(ndev, 0x1002c4, 0, 1 << 20);

			/* Reset the DLLs */
			nv_mask(ndev, 0x1002c0, 0, 1 << 8);
		}
	}

	ptimer->init(ndev, NVDEV_SUBDEV_TIMER);

	kfree(state);
	return 0;
}

int
nv04_clock_create(struct nouveau_device *ndev, int subdev)
{
	struct nv04_clock_priv *priv;
	int ret;

	ret = nouveau_subdev_create(ndev, subdev, "CLK", "clocks", &priv);
	if (ret)
		return ret;

	priv->base.perf_get = nv04_clocks_get;
	priv->base.perf_pre = nv04_clocks_pre;
	priv->base.perf_set = nv04_clocks_set;
	return nouveau_subdev_init(ndev, subdev, ret);
}
