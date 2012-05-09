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

struct nvd0_gpio_priv {
	struct nouveau_gpio base;
};

int
nvd0_gpio_drive(struct nouveau_device *ndev, int line, int dir, int out)
{
	u32 data = ((dir ^ 1) << 13) | (out << 12);
	nv_mask(ndev, 0x00d610 + (line * 4), 0x00003000, data);
	nv_mask(ndev, 0x00d604, 0x00000001, 0x00000001); /* update? */
	return 0;
}

int
nvd0_gpio_sense(struct nouveau_device *ndev, int line)
{
	return !!(nv_rd32(ndev, 0x00d610 + (line * 4)) & 0x00004000);
}

int
nvd0_gpio_create(struct nouveau_device *ndev, int subdev)
{
	struct nvd0_gpio_priv *priv;
	int ret;

	ret = nouveau_subdev_create(ndev, subdev, "GPIO", "gpio", &priv);
	if (ret)
		return ret;

	priv->base.base.destroy = nv50_gpio_destroy;
	priv->base.base.init = nv50_gpio_init;
	priv->base.base.fini = nv50_gpio_fini;
	priv->base.drive = nvd0_gpio_drive;
	priv->base.sense = nvd0_gpio_sense;
	priv->base.irq_enable = nv50_gpio_irq_enable;

	nouveau_irq_register(ndev, 21, nv50_gpio_isr);
	return nouveau_subdev_init(ndev, subdev, ret ? ret : 1);
}
