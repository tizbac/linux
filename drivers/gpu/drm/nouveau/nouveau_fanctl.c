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
#include "nouveau_gpio.h"
#include "nouveau_fanctl.h"
#include "nouveau_therm.h"

int
nouveau_fanctl_create(struct nouveau_device *ndev)
{
	struct nouveau_therm *ptherm = nv_subdev(ndev, NVDEV_SUBDEV_THERM);
	const int subdev = NVDEV_SUBDEV_FAN0;
	const char *fan_s = "PWM";
	int ret;

	/* TODO: handle this mess */
	if (ptherm->fan.type == FAN_I2C) {
		NV_INFO(ndev, "FANCTL: use the onboard %s\n",
			ptherm->fan.i2c_fan->name);
		return -ENODEV;
	}

	switch (ndev->card_type) {
	case NV_40:
		ret = nv40_fantoggle_create(ndev, subdev);
		if (ret)
			ret = nv40_fanpwm_create(ndev, subdev);
		else
			fan_s = "TOGGLE";
		break;
	case NV_50:
	case NV_C0:
		ret = nv50_fanpwm_create(ndev, subdev);
		break;
	default:
		ret = -ENODEV;
		break;
	}

	if (ret == 0)
		NV_INFO(ndev, "FANCTL: using %s fan control\n", fan_s);
	else
		NV_INFO(ndev, "FANCTL: no controllable fan available\n");

	return 0;
}
