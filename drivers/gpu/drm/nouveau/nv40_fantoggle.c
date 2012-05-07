/*
 * Copyright 2012 Nouveau community
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
 * Authors: Martin Peres
 */

#include "drmP.h"

#include "nouveau_drv.h"
#include "nouveau_gpio.h"
#include "nouveau_timer.h"
#include "nouveau_fanctl.h"
#include "nouveau_pm.h"

struct nv40_fantoggle_priv {
	struct nouveau_fanctl base;
	int line;
	u32 divs;

	u8 fanspeed;
};

static int
nv40_fantoggle_get(struct nouveau_fanctl *pfan)
{
	struct nv40_fantoggle_priv *priv = (void *)pfan;

	return priv->fanspeed;
}

static int
nv40_fantoggle_set(struct nouveau_fanctl *pfan, int percent)
{
	struct nv40_fantoggle_priv *priv = (void *)pfan;

	priv->fanspeed = percent;

	return 0;
}

int
nv40_fantoggle_create(struct nouveau_device *ndev, int subdev)
{
	struct nv40_fantoggle_priv *priv;
	struct gpio_func gpio;
	u8 *perf, version;
	u16 divs;
	int ret;

	ret = nouveau_gpio_find(ndev, 0, DCB_GPIO_PWM_FAN, 0xff, &gpio);
	if (ret) {
		NV_DEBUG(ndev, "FANCTL: GPIO tag for TOGGLE not found\n");
		return ret;
	}

	if (gpio.param != 0) {
		NV_DEBUG(ndev, "FANCTL: not a TOGGLE fan\n");
		return -ENODEV;
	}

	if (gpio.line != 2 && gpio.line != 9) {
		NV_ERROR(ndev, "FANCTL: TOGGLE on unexpected GPIO pin\n");
		return -ENODEV;
	}

	perf = nouveau_perf_table(ndev, &version);
	divs = ROM16(perf[6]);
	if (!perf || !divs) {
		NV_ERROR(ndev, "FANCTL: TOGGLE divider unknown\n");
		return -EINVAL;
	}

	ret = nouveau_subdev_create(ndev, subdev, "FANCTL", "fan", &priv);
	if (ret)
		return ret;

	priv->base.get = nv40_fantoggle_get;
	priv->base.set = nv40_fantoggle_set;
	priv->base.sense = nv40_fanpwm_sense;
	priv->line = gpio.line;
	priv->divs = divs;
	return nouveau_subdev_init(ndev, subdev, ret);
}
