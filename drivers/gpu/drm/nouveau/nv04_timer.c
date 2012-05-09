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
#include "nouveau_hw.h"
#include "nouveau_timer.h"

struct nv04_timer_priv {
	struct nouveau_timer base;
};

static u64
nv04_timer_read(struct nouveau_timer *ptimer)
{
	struct nouveau_device *ndev = ptimer->base.device;
	u32 hi, lo;

	do {
		hi = nv_rd32(ndev, NV04_PTIMER_TIME_1);
		lo = nv_rd32(ndev, NV04_PTIMER_TIME_0);
	} while (hi != nv_rd32(ndev, NV04_PTIMER_TIME_1));

	return ((u64)hi << 32 | lo);
}

static int
nv04_timer_init(struct nouveau_device *ndev, int subdev)
{
	u32 m, n, d;

	nv_wr32(ndev, NV04_PTIMER_INTR_EN_0, 0x00000000);
	nv_wr32(ndev, NV04_PTIMER_INTR_0, 0xFFFFFFFF);

	/* aim for 31.25MHz, which gives us nanosecond timestamps */
	d = 1000000 / 32;

	/* determine base clock for timer source */
	if (ndev->chipset < 0x40) {
		n = nouveau_hw_get_clock(ndev, PLL_CORE);
	} else
	if (ndev->chipset == 0x40) {
		/*XXX: figure this out */
		n = 0;
	} else {
		n = ndev->crystal;
		m = 1;
		while (n < (d * 2)) {
			n += (n / m);
			m++;
		}

		nv_wr32(ndev, 0x009220, m - 1);
	}

	if (!n) {
		NV_WARN(ndev, "PTIMER: unknown input clock freq\n");
		if (!nv_rd32(ndev, NV04_PTIMER_NUMERATOR) ||
		    !nv_rd32(ndev, NV04_PTIMER_DENOMINATOR)) {
			nv_wr32(ndev, NV04_PTIMER_NUMERATOR, 1);
			nv_wr32(ndev, NV04_PTIMER_DENOMINATOR, 1);
		}
		return 0;
	}

	/* reduce ratio to acceptable values */
	while (((n % 5) == 0) && ((d % 5) == 0)) {
		n /= 5;
		d /= 5;
	}

	while (((n % 2) == 0) && ((d % 2) == 0)) {
		n /= 2;
		d /= 2;
	}

	while (n > 0xffff || d > 0xffff) {
		n >>= 1;
		d >>= 1;
	}

	nv_wr32(ndev, NV04_PTIMER_NUMERATOR, n);
	nv_wr32(ndev, NV04_PTIMER_DENOMINATOR, d);
	return 0;
}

int
nv04_timer_create(struct nouveau_device *ndev, int subdev)
{
	struct nv04_timer_priv *priv;
	int ret;

	ret = nouveau_subdev_create(ndev, subdev, "PTIMER", "timer", &priv);
	if (ret)
		return ret;

	priv->base.base.init = nv04_timer_init;
	priv->base.read = nv04_timer_read;
	return nouveau_subdev_init(ndev, subdev, ret);
}
