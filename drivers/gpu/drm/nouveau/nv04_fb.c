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
#include "nouveau_fb.h"

struct nv04_fb_priv {
	struct nouveau_fb base;
};

bool
nv04_fb_memtype_valid(struct nouveau_fb *pfb, u32 tile_flags)
{
	if (!(tile_flags & NOUVEAU_GEM_TILE_LAYOUT_MASK))
		return true;

	return false;
}

static int
nv04_fb_init(struct nouveau_device *ndev, int subdev)
{
	/* This is what the DDX did for NV_ARCH_04, but a mmio-trace shows
	 * nvidia reading PFB_CFG_0, then writing back its original value.
	 * (which was 0x701114 in this case)
	 */

	nv_wr32(ndev, NV04_PFB_CFG0, 0x1114);
	return 0;
}

int
nv04_fb_create(struct nouveau_device *ndev, int subdev)
{
	struct nv04_fb_priv *priv;
	u32 boot0;
	int ret;

	ret = nouveau_subdev_create(ndev, subdev, "PFB", "fb", &priv);
	if (ret)
		return ret;

	boot0 = nv_rd32(ndev, NV04_PFB_BOOT_0);
	if (boot0 & 0x00000100) {
		ndev->vram_size  = ((boot0 >> 12) & 0xf) * 2 + 2;
		ndev->vram_size *= 1024 * 1024;
	} else {
		switch (boot0 & NV04_PFB_BOOT_0_RAM_AMOUNT) {
		case NV04_PFB_BOOT_0_RAM_AMOUNT_32MB:
			ndev->vram_size = 32 * 1024 * 1024;
			break;
		case NV04_PFB_BOOT_0_RAM_AMOUNT_16MB:
			ndev->vram_size = 16 * 1024 * 1024;
			break;
		case NV04_PFB_BOOT_0_RAM_AMOUNT_8MB:
			ndev->vram_size = 8 * 1024 * 1024;
			break;
		case NV04_PFB_BOOT_0_RAM_AMOUNT_4MB:
			ndev->vram_size = 4 * 1024 * 1024;
			break;
		}
	}

	if ((boot0 & 0x00000038) <= 0x10)
		ndev->vram_type = NV_MEM_TYPE_SGRAM;
	else
		ndev->vram_type = NV_MEM_TYPE_SDRAM;

	priv->base.base.init = nv04_fb_init;
	priv->base.memtype_valid = nv04_fb_memtype_valid;
	return nouveau_subdev_init(ndev, subdev, ret);
}
