/*
 * Copyright (C) 2009 Francisco Jerez.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_hw.h"
#include "nouveau_gpio.h"

int
nv10_gpio_sense(struct nouveau_device *ndev, int line)
{
	if (line < 2) {
		line = line * 16;
		line = NVReadCRTC(ndev, 0, NV_PCRTC_GPIO) >> line;
		return !!(line & 0x0100);
	} else
	if (line < 10) {
		line = (line - 2) * 4;
		line = NVReadCRTC(ndev, 0, NV_PCRTC_GPIO_EXT) >> line;
		return !!(line & 0x04);
	} else
	if (line < 14) {
		line = (line - 10) * 4;
		line = NVReadCRTC(ndev, 0, NV_PCRTC_850) >> line;
		return !!(line & 0x04);
	}

	return -EINVAL;
}

int
nv10_gpio_drive(struct nouveau_device *ndev, int line, int dir, int out)
{
	u32 reg, mask, data;

	if (line < 2) {
		line = line * 16;
		reg  = NV_PCRTC_GPIO;
		mask = 0x00000011;
		data = (dir << 4) | out;
	} else
	if (line < 10) {
		line = (line - 2) * 4;
		reg  = NV_PCRTC_GPIO_EXT;
		mask = 0x00000003;
		data = (dir << 1) | out;
	} else
	if (line < 14) {
		line = (line - 10) * 4;
		reg  = NV_PCRTC_850;
		mask = 0x00000003;
		data = (dir << 1) | out;
	} else {
		return -EINVAL;
	}

	mask = NVReadCRTC(ndev, 0, reg) & ~(mask << line);
	NVWriteCRTC(ndev, 0, reg, mask | (data << line));
	return 0;
}

void
nv10_gpio_irq_enable(struct nouveau_device *ndev, int line, bool on)
{
	u32 mask = 0x00010001 << line;

	nv_wr32(ndev, 0x001104, mask);
	nv_mask(ndev, 0x001144, mask, on ? mask : 0);
}

static void
nv10_gpio_isr(struct nouveau_device *ndev)
{
	u32 intr = nv_rd32(ndev, 0x1104);
	u32 hi = (intr & 0x0000ffff) >> 0;
	u32 lo = (intr & 0xffff0000) >> 16;

	nouveau_gpio_isr(ndev, 0, hi | lo);

	nv_wr32(ndev, 0x001104, intr);
}

int
nv10_gpio_init(struct nouveau_device *ndev)
{
	nv_wr32(ndev, 0x001140, 0x00000000);
	nv_wr32(ndev, 0x001100, 0xffffffff);
	nv_wr32(ndev, 0x001144, 0x00000000);
	nv_wr32(ndev, 0x001104, 0xffffffff);
	nouveau_irq_register(ndev, 28, nv10_gpio_isr); /* PBUS */
	return 0;
}

void
nv10_gpio_fini(struct nouveau_device *ndev)
{
	nv_wr32(ndev, 0x001140, 0x00000000);
	nv_wr32(ndev, 0x001144, 0x00000000);
	nouveau_irq_unregister(ndev, 28);
}
