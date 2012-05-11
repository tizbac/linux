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
 * 	    Martin Peres
 */

#include "drmP.h"

#include "nouveau_drv.h"
#include "nouveau_gpio.h"
#include "nouveau_timer.h"
#include "nouveau_fanctl.h"
#include "nouveau_pm.h"

struct nv50_fanpwm_priv {
	struct nouveau_fanctl base;
	bool invert;
	u32 ctrl;
	u32 divs;
	u8  line;
	u8  indx;
};

static int
nv50_fanpwm_get(struct nouveau_fanctl *pfan)
{
	struct nouveau_device *ndev = pfan->base.device;
	struct nv50_fanpwm_priv *priv = (void *)pfan;

	if (nv_rd32(ndev, priv->ctrl) & (1 << priv->line)) {
		u32 divs = nv_rd32(ndev, 0x00e114 + (priv->indx * 8));
		u32 duty = nv_rd32(ndev, 0x00e118 + (priv->indx * 8));

		if (priv->invert)
			duty = divs - duty;
		return (duty * 100) / divs;
	}

	return -EINVAL;
}

static int
nv50_fanpwm_set(struct nouveau_fanctl *pfan, int percent)
{
	struct nouveau_device *ndev = pfan->base.device;
	struct nv50_fanpwm_priv *priv = (void *)pfan;
	u32 line = priv->line;
	u32 duty;

	duty = ((priv->divs * percent) + 99) / 100;
	if (priv->invert)
		duty = priv->divs - duty;

	nv_mask(ndev, priv->ctrl, 0x00010001 << line, 0x00000001 << line);
	nv_wr32(ndev, 0x00e114 + (priv->indx * 8), priv->divs);
	nv_wr32(ndev, 0x00e118 + (priv->indx * 8), 0x80000000 | duty);
	return 0;
}

int
nv50_fanpwm_create(struct nouveau_device *ndev, int subdev)
{
	struct nouveau_pm_engine *pm = &ndev->subsys.pm;
	struct nv50_fanpwm_priv *priv;
	struct gpio_func gpio;
	u32 ctrl, line, indx;
	u8 *perf, version;
	u16 divs = 0;
	int ret;

	ret = nouveau_gpio_find(ndev, 0, DCB_GPIO_PWM_FAN, 0xff, &gpio);
	if (ret) {
		NV_DEBUG(ndev, "FANCTL: GPIO tag for PWM not found\n");
		return ret;
	}

	switch (gpio.line) {
	case 0x04:
		ctrl = 0x00e100;
		line = 4;
		indx = 0;
		break;
	case 0x09:
		ctrl = 0x00e100;
		line = 9;
		indx = 1;
		break;
	case 0x10:
		ctrl = 0x00e280;
		line = 0;
		indx = 0;
		break;
	default:
		NV_ERROR(ndev, "FANCTL: PWM on unexpected GPIO pin\n");
		return -ENODEV;
	}

	/*XXX: FIXME, mupuf's on it ;) */
	if ((perf = nouveau_perf_table(ndev, &version)) && version < 0x40) {
		divs = ROM16(perf[6]);
	} else {
		if (pm->fan.pwm_freq) {
			divs = 135000 / pm->fan.pwm_freq;
			if (ndev->chipset < 0xa3)
				divs /= 4;
		}
	}

	if (!divs) {
		NV_ERROR(ndev, "FANCTL: PWM divider unknown\n");
		return -EINVAL;
	}

	ret = nouveau_subdev_create(ndev, subdev, "FANCTL", "fan", &priv);
	if (ret)
		return ret;

	priv->base.get = nv50_fanpwm_get;
	priv->base.set = nv50_fanpwm_set;
	priv->base.sense = nv40_fanpwm_sense;
	priv->invert = gpio.log[0] & 1;
	priv->ctrl = ctrl;
	priv->line = line;
	priv->indx = indx;
	priv->divs = divs;
	return nouveau_subdev_init(ndev, subdev, ret);
}
