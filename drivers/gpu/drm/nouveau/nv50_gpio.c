/*
 * Copyright 2010 Red Hat Inc.
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
#include "nouveau_hw.h"
#include "nouveau_gpio.h"

struct nv50_gpio_priv {
	struct nouveau_gpio base;
};

static int
nv50_gpio_location(int line, u32 *reg, u32 *shift)
{
	const u32 nv50_gpio_reg[4] = { 0xe104, 0xe108, 0xe280, 0xe284 };

	if (line >= 32)
		return -EINVAL;

	*reg = nv50_gpio_reg[line >> 3];
	*shift = (line & 7) << 2;
	return 0;
}

static int
nv50_gpio_drive(struct nouveau_device *ndev, int line, int dir, int out)
{
	u32 reg, shift;

	if (nv50_gpio_location(line, &reg, &shift))
		return -EINVAL;

	nv_mask(ndev, reg, 7 << shift, (((dir ^ 1) << 1) | out) << shift);
	return 0;
}

static int
nv50_gpio_sense(struct nouveau_device *ndev, int line)
{
	u32 reg, shift;

	if (nv50_gpio_location(line, &reg, &shift))
		return -EINVAL;

	return !!(nv_rd32(ndev, reg) & (4 << shift));
}

void
nv50_gpio_irq_enable(struct nouveau_device *ndev, int line, bool on)
{
	u32 reg  = line < 16 ? 0xe050 : 0xe070;
	u32 mask = 0x00010001 << (line & 0xf);

	nv_wr32(ndev, reg + 4, mask);
	nv_mask(ndev, reg + 0, mask, on ? mask : 0);
}

void
nv50_gpio_isr(struct nouveau_device *ndev)
{
	u32 intr0, intr1 = 0;
	u32 hi, lo;

	intr0 = nv_rd32(ndev, 0xe054) & nv_rd32(ndev, 0xe050);
	if (ndev->chipset >= 0x90)
		intr1 = nv_rd32(ndev, 0xe074) & nv_rd32(ndev, 0xe070);

	hi = (intr0 & 0x0000ffff) | (intr1 << 16);
	lo = (intr0 >> 16) | (intr1 & 0xffff0000);
	nouveau_gpio_isr(ndev, 0, hi | lo);

	nv_wr32(ndev, 0xe054, intr0);
	if (ndev->chipset >= 0x90)
		nv_wr32(ndev, 0xe074, intr1);
}

int
nv50_gpio_init(struct nouveau_device *ndev, int subdev)
{
	/* disable, and ack any pending gpio interrupts */
	nv_wr32(ndev, 0xe050, 0x00000000);
	nv_wr32(ndev, 0xe054, 0xffffffff);
	if (ndev->chipset >= 0x90) {
		nv_wr32(ndev, 0xe070, 0x00000000);
		nv_wr32(ndev, 0xe074, 0xffffffff);
	}

	return 0;
}

int
nv50_gpio_fini(struct nouveau_device *ndev, int subdev, bool suspend)
{
	nv_wr32(ndev, 0xe050, 0x00000000);
	if (ndev->chipset >= 0x90)
		nv_wr32(ndev, 0xe070, 0x00000000);
	return 0;
}

void
nv50_gpio_destroy(struct nouveau_device *ndev, int subdev)
{
	nouveau_irq_unregister(ndev, 21);
}

int
nv50_gpio_create(struct nouveau_device *ndev, int subdev)
{
	struct nv50_gpio_priv *priv;
	int ret;

	ret = nouveau_subdev_create(ndev, subdev, "GPIO", "gpio", &priv);
	if (ret)
		return ret;

	priv->base.base.destroy = nv50_gpio_destroy;
	priv->base.base.init = nv50_gpio_init;
	priv->base.base.fini = nv50_gpio_fini;
	priv->base.drive = nv50_gpio_drive;
	priv->base.sense = nv50_gpio_sense;
	priv->base.irq_enable = nv50_gpio_irq_enable;

	nouveau_irq_register(ndev, 21, nv50_gpio_isr);
	return nouveau_subdev_init(ndev, subdev, ret ? ret : 1);
}
