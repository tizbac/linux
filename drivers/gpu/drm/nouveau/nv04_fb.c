#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"

int
nv04_fb_vram_init(struct drm_device *dev)
{
	struct nouveau_device *ndev = nouveau_device(dev);
	u32 boot0 = nv_rd32(dev, NV04_PFB_BOOT_0);

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

	return 0;
}

int
nv04_fb_init(struct drm_device *dev)
{
	/* This is what the DDX did for NV_ARCH_04, but a mmio-trace shows
	 * nvidia reading PFB_CFG_0, then writing back its original value.
	 * (which was 0x701114 in this case)
	 */

	nv_wr32(dev, NV04_PFB_CFG0, 0x1114);
	return 0;
}

void
nv04_fb_takedown(struct drm_device *dev)
{
}
