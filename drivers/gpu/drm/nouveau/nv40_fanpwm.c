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

struct nv40_fanpwm_priv {
	struct nouveau_fanctl base;
	int line;
	u32 divs;
};

static int
nv40_fanpwm_get(struct nouveau_fanctl *pfan)
{
	struct nouveau_device *ndev = pfan->base.device;
	struct nv40_fanpwm_priv *priv = (void *)pfan;
	u32 data, duty, divs;

	if (priv->line == 2) {
		data = nv_rd32(ndev, 0x0010f0);
		duty = (data & 0x7fff0000) >> 16;
		divs = (data & 0x00007fff);
	} else {
		data = nv_rd32(ndev, 0x0015f4);
		divs = nv_rd32(ndev, 0x0015f8);
		duty = (data & 0x7fffffff);
	}

	if (!(data & 0x80000000) || !divs)
		return -EINVAL;

	return ((divs - duty) * 100) / divs;
}

static int
nv40_fanpwm_set(struct nouveau_fanctl *pfan, int percent)
{
	struct nouveau_device *ndev = pfan->base.device;
	struct nv40_fanpwm_priv *priv = (void *)pfan;
	u32 duty;

	duty = ((priv->divs * percent) + 99) / 100;
	duty = priv->divs - duty;

	if (priv->line == 2) {
		nv_wr32(ndev, 0x0010f0, 0x80000000 | (duty << 16) | priv->divs);
	} else {
		nv_wr32(ndev, 0x0015f8, priv->divs);
		nv_wr32(ndev, 0x0015f4, 0x80000000 | duty);
	}

	return 0;
}

int
nv40_fanpwm_sense(struct nouveau_fanctl *pfan)
{
	struct nouveau_device *ndev = pfan->base.device;
	struct nouveau_timer *ptimer = nv_subdev(ndev, NVDEV_SUBDEV_TIMER);
	struct gpio_func gpio;
	u32 cycles, cur, prev;
	u64 start, end, tach;
	int ret;

	ret = nouveau_gpio_find(ndev, 0, DCB_GPIO_FAN_SENSE, 0xff, &gpio);
	if (ret)
		return ret;

	/* Time a complete rotation and extrapolate to RPM:
	 * When the fan spins, it changes the value of GPIO FAN_SENSE.
	 * We get 4 changes (0 -> 1 -> 0 -> 1) per complete rotation.
	 */
	start = ptimer->read(ptimer);
	prev = nouveau_gpio_func_get(ndev, gpio.func);
	cycles = 0;
	do {
		usleep_range(500, 1000); /* supports 0 < rpm < 7500 */

		cur = nouveau_gpio_func_get(ndev, gpio.func);
		if (prev != cur) {
			if (!start)
				start = ptimer->read(ptimer);
			cycles++;
			prev = cur;
		}
	} while (cycles < 5 && ptimer->read(ptimer) - start < 250000000);
	end = ptimer->read(ptimer);

	if (cycles == 5) {
		tach = (u64)60000000000;
		do_div(tach, (end - start));
		return tach;
	} else
		return 0;
}

int
nv40_fanpwm_create(struct nouveau_device *ndev, int subdev)
{
	struct nv40_fanpwm_priv *priv;
	struct gpio_func gpio;
	u8 *perf, version;
	u16 divs;
	int ret;

	ret = nouveau_gpio_find(ndev, 0, DCB_GPIO_PWM_FAN, 0xff, &gpio);
	if (ret) {
		NV_DEBUG(ndev, "FANCTL: GPIO tag for PWM not found\n");
		return ret;
	}

	if (gpio.param != 1) {
		NV_DEBUG(ndev, "FANCTL: not a PWM fan\n");
		return -ENODEV;
	}

	if (gpio.line != 2 && gpio.line != 9) {
		NV_ERROR(ndev, "FANCTL: PWM on unexpected GPIO pin\n");
		return -ENODEV;
	}

	perf = nouveau_perf_table(ndev, &version);
	if (!perf || !(divs = ROM16(perf[6]))) {
		NV_ERROR(ndev, "FANCTL: PWM divider unknown\n");
		return -EINVAL;
	}

	ret = nouveau_subdev_create(ndev, subdev, "FANCTL", "fan", &priv);
	if (ret)
		return ret;

	priv->base.get = nv40_fanpwm_get;
	priv->base.set = nv40_fanpwm_set;
	priv->base.sense = nv40_fanpwm_sense;
	priv->line = gpio.line;
	priv->divs = divs;
	return nouveau_subdev_init(ndev, subdev, ret);
}
