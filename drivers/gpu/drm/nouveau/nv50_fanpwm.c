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
#include "nouveau_therm.h"

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
	struct nouveau_therm *ptherm = nv_subdev(ndev, NVDEV_SUBDEV_THERM);
	struct nv50_fanpwm_priv *priv;
	struct gpio_func gpio;
	u32 ctrl, line, indx;
	u8 *perf, version;
	u16 divs = 0, freq = 0;
	u32 pwm_clock = 0;
	int ret;

	ret = nouveau_gpio_find(ndev, 0, DCB_GPIO_PWM_FAN, 0xff, &gpio);
	if (ret) {
		NV_DEBUG(ndev, "FANCTL: GPIO tag for PWM not found\n");
		return ret;
	}

	if (ndev->chipset == 0x50) {
		NV_ERROR(ndev, "FANCTL: PWM on nv50 is unsupported\n");
		return -ENODEV;
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

	perf = nouveau_perf_table(ndev, &version);
	if (perf && version < 0x40)
		divs = ROM16(perf[6]);

	if (ptherm->fan.pwm_freq)
		freq = ptherm->fan.pwm_freq;
	else if (!divs) {
		NV_ERROR(ndev, "FANCTL: unknown PWM freq. Set to 2500 Hz\n");
		freq = 2500;
	}

	if (freq) {
		/* determine the PWM source clock */
		if (ndev->chipset > 0x50 && ndev->chipset < 0x94) {
			u8 pwm_div = nv_rd32(ndev, 0x410c);
			if (nv_rd32(ndev, 0xc040) & 0x800000) {
				/* Use the HOST clock (100 MHz)
				* Where does this constant(2.4) comes from? */
				pwm_clock = (100000000 >> pwm_div) / 2.4;
			} else {
				/* Where does this constant(20) comes from? */
				pwm_clock = (ndev->crystal * 1000) >> pwm_div;
				pwm_clock /= 20;
			}
		} else {
			pwm_clock = (ndev->crystal * 1000) / 20;
		}

		divs = pwm_clock / freq;
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
