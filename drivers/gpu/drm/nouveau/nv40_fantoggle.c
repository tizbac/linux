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
	struct nouveau_alarm alarm;
	int line;

	u32 period_us;
	u8 fanspeed;
};

static void
nv40_fantoggle_timer_callback(struct nouveau_alarm *alarm)
{
	struct nv40_fantoggle_priv *priv =
		container_of(alarm, struct nv40_fantoggle_priv, alarm);
	struct nouveau_device *ndev = priv->base.base.device;
	struct nouveau_timer *ptimer = nv_subdev(ndev, NVDEV_SUBDEV_TIMER);
	int duty = 0, next_change = 0;

	duty = !nouveau_gpio_func_get(ndev, DCB_GPIO_PWM_FAN);
	nouveau_gpio_func_set(ndev, DCB_GPIO_PWM_FAN, duty);

	next_change = (priv->fanspeed * priv->period_us) / 100;
	if (duty)
		next_change = priv->period_us - next_change;

	if (priv->fanspeed > 0 && priv->fanspeed < 100)
		ptimer->alarm(ptimer, next_change * 1000, alarm);
}

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
	int prev_fanspeed = priv->fanspeed;

	priv->fanspeed = percent;

	if (prev_fanspeed == 0 || prev_fanspeed == 100)
		nv40_fantoggle_timer_callback(&priv->alarm);

	return 0;
}

int
nv40_fantoggle_create(struct nouveau_device *ndev, int subdev)
{
	struct nv40_fantoggle_priv *priv;
	struct gpio_func gpio;
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


	ret = nouveau_subdev_create(ndev, subdev, "FANCTL", "fan", &priv);
	if (ret)
		return ret;

	priv->base.get = nv40_fantoggle_get;
	priv->base.set = nv40_fantoggle_set;
	priv->base.sense = nv40_fanpwm_sense;
	priv->alarm.func = nv40_fantoggle_timer_callback;
	priv->line = gpio.line;
	priv->period_us = 28328; /* use adt7473's default PWM frequency */
	priv->fanspeed = 30; /* TODO: use pm->cur->fanspeed */

	nv40_fantoggle_timer_callback(&priv->alarm);

	return nouveau_subdev_init(ndev, subdev, ret);
}
