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

#include "nouveau_device.h"
#include "nouveau_bios.h"
#include "nouveau_mc.h"
#include "nouveau_timer.h"
#include "nouveau_fb.h"
#include "nouveau_instmem.h"
#include "nouveau_gpuobj.h"
#include "nouveau_bar.h"
#include "nouveau_gpio.h"
#include "nouveau_volt.h"
#include "nouveau_fanctl.h"
#include "nouveau_clock.h"
#include "nouveau_therm.h"

int
nouveau_device_init(struct nouveau_device *ndev)
{
	int i, ret = 0;

	for (i = 0; i < NVDEV_SUBDEV_NR; i++) {
		ret = nouveau_subdev_init(ndev, i, 0);
		if (ret)
			goto error;
	}

error:
	for (--i; ret && i >= 0; i--)
		nouveau_subdev_fini(ndev, i, false);
	return ret;
}

int
nouveau_device_fini(struct nouveau_device *ndev, bool suspend)
{
	int i, ret = 0;

	for (i = NVDEV_SUBDEV_NR - 1; i >= 0; i--) {
		ret = nouveau_subdev_fini(ndev, i, suspend);
		if (ret)
			goto error;
	}

error:
	for (--i; ret && i >= 0; i--)
		nouveau_subdev_init(ndev, i, 0);
	return ret;
}

void
nouveau_device_destroy(struct nouveau_device *ndev)
{
	int i;

	for (i = NVDEV_SUBDEV_NR - 1; i >= 0; i--)
		nouveau_subdev_destroy(ndev, i);
}

int
nouveau_device_create(struct nouveau_device *ndev)
{
	int ret = 0;

	/* video bios parsing and init table execution */
	switch (ndev->card_type * !ret) {
	case NV_04:
	case NV_10:
	case NV_20:
	case NV_30:
	case NV_40:
	case NV_50:
	case NV_C0:
	case NV_D0:
	case NV_E0:
		ret = nouveau_bios_create(ndev, NVDEV_SUBDEV_VBIOS);
		break;
	default:
		break;
	}

	/* GPIO functions and interrupt handling */
	switch (ndev->card_type * !ret) {
	case NV_04:
	case NV_10:
	case NV_20:
	case NV_30:
	case NV_40:
	case NV_50:
	case NV_C0:
	case NV_D0:
	case NV_E0:
		ret = nouveau_gpio_create(ndev, NVDEV_SUBDEV_GPIO);
		break;
	default:
		break;
	}

	/* initialise bios/gpio now, they have inter-dependencies so it's
	 * not done immediately after they're created
	 */
	if (ret == 0)
		ret = nouveau_device_init(ndev);

	/* master control */
	switch (ndev->card_type * !ret) {
	case NV_04:
	case NV_10:
	case NV_20:
	case NV_30:
		ret = nv04_mc_create(ndev, NVDEV_SUBDEV_MC);
		break;
	case NV_40:
		ret = nv40_mc_create(ndev, NVDEV_SUBDEV_MC);
		break;
	case NV_50:
	case NV_C0:
	case NV_D0:
	case NV_E0:
		ret = nv50_mc_create(ndev, NVDEV_SUBDEV_MC);
		break;
	default:
		break;
	}

	/* hardware timer and alarms */
	switch (ndev->card_type * !ret) {
	case NV_04:
	case NV_10:
	case NV_20:
	case NV_30:
	case NV_40:
	case NV_50:
	case NV_C0:
	case NV_D0:
	case NV_E0:
		ret = nv04_timer_create(ndev, NVDEV_SUBDEV_TIMER);
		break;
	default:
		break;
	}

	/* memory interface */
	switch (ndev->card_type * !ret) {
	case NV_04:
		ret = nv04_fb_create(ndev, NVDEV_SUBDEV_FB);
		break;
	case NV_10:
		ret = nv10_fb_create(ndev, NVDEV_SUBDEV_FB);
		break;
	case NV_20:
		ret = nv20_fb_create(ndev, NVDEV_SUBDEV_FB);
		break;
	case NV_30:
		ret = nv30_fb_create(ndev, NVDEV_SUBDEV_FB);
		break;
	case NV_40:
		ret = nv40_fb_create(ndev, NVDEV_SUBDEV_FB);
		break;
	case NV_50:
		ret = nv50_fb_create(ndev, NVDEV_SUBDEV_FB);
		break;
	case NV_C0:
	case NV_D0:
	case NV_E0:
		ret = nvc0_fb_create(ndev, NVDEV_SUBDEV_FB);
		break;
	default:
		break;
	}

	/* kernel gpu memory objects and mapping */
	switch (ndev->card_type * !ret) {
	case NV_04:
	case NV_10:
	case NV_20:
	case NV_30:
	case NV_40:
		ret = nv04_instmem_create(ndev, NVDEV_SUBDEV_INSTMEM);
		break;
	case NV_50:
		ret = nv50_instmem_create(ndev, NVDEV_SUBDEV_INSTMEM);
		break;
	case NV_C0:
	case NV_D0:
	case NV_E0:
		ret = nvc0_instmem_create(ndev, NVDEV_SUBDEV_INSTMEM);
		break;
	default:
		break;
	}

	switch (ndev->card_type * !ret) {
	case NV_04:
	case NV_10:
	case NV_20:
	case NV_30:
	case NV_40:
	case NV_50:
	case NV_C0:
	case NV_D0:
	case NV_E0:
		ret = nouveau_gpuobj_create(ndev, NVDEV_SUBDEV_GPUOBJ);
		break;
	default:
		break;
	}

	/* FB BAR allocation and management */
	switch (ndev->card_type * !ret) {
	case NV_50:
		ret = nv50_bar_create(ndev, NVDEV_SUBDEV_BAR);
		break;
	case NV_C0:
	case NV_D0:
	case NV_E0:
		ret = nvc0_bar_create(ndev, NVDEV_SUBDEV_BAR);
		break;
	default:
		break;
	}

	/* voltage control */
	switch (ndev->card_type * !ret) {
	case NV_04:
	case NV_10:
	case NV_20:
	case NV_30:
	case NV_40:
	case NV_50:
	case NV_C0:
	case NV_D0:
	case NV_E0:
		ret = nouveau_volt_create(ndev, NVDEV_SUBDEV_VOLT);
		break;
	default:
		break;
	}

	/* thermal management */
	switch (ndev->card_type * !ret) {
	case NV_04:
	case NV_10:
	case NV_20:
	case NV_30:
	case NV_40:
	case NV_50:
	case NV_C0:
	case NV_D0:
	case NV_E0:
		ret = nouveau_therm_create(ndev, NVDEV_SUBDEV_THERM);
		break;
	default:
		break;
	}

	/* fan control */
	switch (ndev->card_type * !ret) {
	case NV_04:
	case NV_10:
	case NV_20:
	case NV_30:
	case NV_40:
	case NV_50:
	case NV_C0:
	case NV_D0:
	case NV_E0:
		ret = nouveau_fanctl_create(ndev);
		break;
	default:
		break;
	}

	/* clock control */
	switch (ndev->card_type * !ret) {
	case NV_04:
	case NV_10:
	case NV_20:
	case NV_30:
		ret = nv04_clock_create(ndev, NVDEV_SUBDEV_CLOCK);
		break;
	case NV_40:
		ret = nv40_clock_create(ndev, NVDEV_SUBDEV_CLOCK);
		break;
	case NV_50:
		if (ndev->chipset <= 0x98)
			ret = nv50_clock_create(ndev, NVDEV_SUBDEV_CLOCK);
		else
		if (ndev->chipset != 0xaa && ndev->chipset != 0xac)
			ret = nva3_clock_create(ndev, NVDEV_SUBDEV_CLOCK);
		break;
	case NV_C0:
		ret = nvc0_clock_create(ndev, NVDEV_SUBDEV_CLOCK);
		break;
	default:
		break;
	}

	if (ret)
		nouveau_device_destroy(ndev);
	return ret;
}
