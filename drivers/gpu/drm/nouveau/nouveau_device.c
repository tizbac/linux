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
#include "nouveau_fifo.h"
#include "nouveau_graph.h"
#include "nouveau_software.h"
#include "nouveau_crypt.h"
#include "nouveau_copy.h"
#include "nouveau_mpeg.h"
#include "nouveau_bsp.h"
#include "nouveau_vp.h"
#include "nouveau_ppp.h"

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
	int disable = nouveau_noaccel;
	int ret = 0;

	/* mask out any engines that are known not to work as they should,
	 * these can be overridden by the user
	 */
	if (disable == -1) {
		switch (ndev->chipset) {
		case 0xd9: /* known broken without binary driver firmware */
		case 0xe4: /* needs binary driver firmware */
		case 0xe7: /* needs binary driver firmware */
			disable = 1 << NVDEV_ENGINE_GR;
			break;
		default:
			disable = 0;
			break;
		}

		if (disable) {
			NV_INFO(ndev, "some engines disabled by default, pass "
				      "noaccel=0 to force enable\n");
		}
	}

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

	/* handle master "disable all accel" switch */
	if ((disable & 1) || ret)
		disable = ~0;

	/* FIFO channels */
	switch (ndev->card_type * !(disable & (1 << NVDEV_ENGINE_FIFO))) {
	case NV_04:
		nv04_fifo_create(ndev, NVDEV_ENGINE_FIFO);
		break;
	case NV_10:
	case NV_20:
	case NV_30:
		if (ndev->chipset < 0x17)
			nv10_fifo_create(ndev, NVDEV_ENGINE_FIFO);
		else
			nv17_fifo_create(ndev, NVDEV_ENGINE_FIFO);
		break;
	case NV_40:
		nv40_fifo_create(ndev, NVDEV_ENGINE_FIFO);
		break;
	case NV_50:
		if (ndev->chipset == 0x50)
			nv50_fifo_create(ndev, NVDEV_ENGINE_FIFO);
		else
			nv84_fifo_create(ndev, NVDEV_ENGINE_FIFO);
		break;
	case NV_C0:
	case NV_D0:
		nvc0_fifo_create(ndev, NVDEV_ENGINE_FIFO);
		break;
	case NV_E0:
		nve0_fifo_create(ndev, NVDEV_ENGINE_FIFO);
		break;
	default:
		break;
	}

	/* software engine */
	switch (ndev->card_type * !(disable & (1 << NVDEV_ENGINE_SW))) {
	case NV_04:
	case NV_10:
	case NV_20:
	case NV_30:
	case NV_40:
		nv04_software_create(ndev, NVDEV_ENGINE_SW);
		break;
	case NV_50:
		nv50_software_create(ndev, NVDEV_ENGINE_SW);
		break;
	case NV_C0:
	case NV_D0:
	case NV_E0:
		nvc0_software_create(ndev, NVDEV_ENGINE_SW);
		break;
	default:
		break;
	}

	/* graphics/compute engine */
	switch (ndev->card_type * !(disable & (1 << NVDEV_ENGINE_GR))) {
	case NV_04:
		nv04_graph_create(ndev, NVDEV_ENGINE_GR);
		break;
	case NV_10:
		nv10_graph_create(ndev, NVDEV_ENGINE_GR);
		break;
	case NV_20:
	case NV_30:
		nv20_graph_create(ndev, NVDEV_ENGINE_GR);
		break;
	case NV_40:
		nv40_graph_create(ndev, NVDEV_ENGINE_GR);
		break;
	case NV_50:
		nv50_graph_create(ndev, NVDEV_ENGINE_GR);
		break;
	case NV_C0:
	case NV_D0:
		nvc0_graph_create(ndev, NVDEV_ENGINE_GR);
		break;
	case NV_E0:
		nve0_graph_create(ndev, NVDEV_ENGINE_GR);
		break;
	default:
		break;
	}

	/* crypto engine */
	switch (ndev->card_type * !(disable & (1 << NVDEV_ENGINE_CRYPT))) {
	case NV_50:
		switch (ndev->chipset) {
		case 0x84:
		case 0x86:
		case 0x92:
		case 0x94:
		case 0x96:
		case 0xa0:
			nv84_crypt_create(ndev, NVDEV_ENGINE_CRYPT);
			break;
		case 0x98:
		case 0xaa:
		case 0xac:
			nv98_crypt_create(ndev, NVDEV_ENGINE_CRYPT);
			break;
		}
		break;
	default:
		break;
	}

	/* copy engine(s) */
	switch (ndev->card_type * !(disable & (1 << NVDEV_ENGINE_COPY0))) {
	case NV_50:
		switch (ndev->chipset) {
		case 0xa3:
		case 0xa5:
		case 0xa8:
		case 0xaf:
			nva3_copy_create(ndev, NVDEV_ENGINE_COPY0);
			break;
		default:
			break;
		}
		break;
	case NV_C0:
	case NV_D0:
		nvc0_copy_create(ndev, NVDEV_ENGINE_COPY0);
		break;
	default:
		break;
	}

	switch (ndev->card_type * !(disable & (1 << NVDEV_ENGINE_COPY1))) {
	case NV_C0:
		nvc0_copy_create(ndev, NVDEV_ENGINE_COPY1);
		break;
	default:
		break;
	}

	/* mpeg decoder */
	switch (ndev->card_type * !(disable & (1 << NVDEV_ENGINE_MPEG))) {
	case NV_30:
		switch (ndev->chipset) {
		case 0x31:
		case 0x34:
		case 0x36:
			nv31_mpeg_create(ndev, NVDEV_ENGINE_MPEG);
			break;
		}
		break;
	case NV_40:
		nv31_mpeg_create(ndev, NVDEV_ENGINE_MPEG);
		break;
	case NV_50:
		if (ndev->chipset < 0x98 || ndev->chipset == 0xa0)
			nv50_mpeg_create(ndev, NVDEV_ENGINE_MPEG);
		break;
	default:
		break;
	}

	/* bitstream processor */
	switch (ndev->card_type * !(disable & (1 << NVDEV_ENGINE_BSP))) {
	case NV_50:
		if (ndev->chipset >= 0x84)
			nv84_bsp_create(ndev, NVDEV_ENGINE_BSP);
		break;
	case NV_C0:
	case NV_D0:
	case NV_E0:
		nvc0_bsp_create(ndev, NVDEV_ENGINE_BSP);
		break;
	default:
		break;
	}

	/* video processor */
	switch (ndev->card_type * !(disable & (1 << NVDEV_ENGINE_VP))) {
	case NV_50:
		if (ndev->chipset >= 0xa3 || ndev->chipset == 0x98)
			nv98_vp_create(ndev, NVDEV_ENGINE_VP);
		break;
	case NV_C0:
	case NV_D0:
	case NV_E0:
		nvc0_vp_create(ndev, NVDEV_ENGINE_VP);
		break;
	default:
		break;
	}

	/* video postprocessing engine */
	switch (ndev->card_type * !(disable & (1 << NVDEV_ENGINE_PPP))) {
	case NV_50:
		if (ndev->chipset >= 0xa3 || ndev->chipset == 0x98)
			nv98_ppp_create(ndev, NVDEV_ENGINE_PPP);
		break;
	case NV_C0:
	case NV_D0:
	case NV_E0:
		nvc0_vp_create(ndev, NVDEV_ENGINE_PPP);
		break;
	default:
		break;
	}

	if (ret)
		nouveau_device_destroy(ndev);
	return ret;
}
