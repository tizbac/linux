#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"

void
nv10_fb_init_tile_region(struct drm_device *dev, int i, uint32_t addr,
			 uint32_t size, uint32_t pitch, uint32_t flags)
{
	struct nouveau_device *ndev = nouveau_device(dev);
	struct nouveau_tile_reg *tile = &ndev->tile.reg[i];

	tile->addr  = 0x80000000 | addr;
	tile->limit = max(1u, addr + size) - 1;
	tile->pitch = pitch;
}

void
nv10_fb_free_tile_region(struct drm_device *dev, int i)
{
	struct nouveau_device *ndev = nouveau_device(dev);
	struct nouveau_tile_reg *tile = &ndev->tile.reg[i];

	tile->addr = tile->limit = tile->pitch = tile->zcomp = 0;
}

void
nv10_fb_set_tile_region(struct drm_device *dev, int i)
{
	struct nouveau_device *ndev = nouveau_device(dev);
	struct nouveau_tile_reg *tile = &ndev->tile.reg[i];

	nv_wr32(dev, NV10_PFB_TLIMIT(i), tile->limit);
	nv_wr32(dev, NV10_PFB_TSIZE(i), tile->pitch);
	nv_wr32(dev, NV10_PFB_TILE(i), tile->addr);
}

int
nv1a_fb_vram_init(struct drm_device *dev)
{
	struct nouveau_device *ndev = nouveau_device(dev);
	struct pci_dev *bridge;
	uint32_t mem, mib;

	bridge = pci_get_bus_and_slot(0, PCI_DEVFN(0, 1));
	if (!bridge) {
		NV_ERROR(dev, "no bridge device\n");
		return 0;
	}

	if (ndev->chipset == 0x1a) {
		pci_read_config_dword(bridge, 0x7c, &mem);
		mib = ((mem >> 6) & 31) + 1;
	} else {
		pci_read_config_dword(bridge, 0x84, &mem);
		mib = ((mem >> 4) & 127) + 1;
	}

	ndev->vram_size = mib * 1024 * 1024;
	return 0;
}

int
nv10_fb_vram_init(struct drm_device *dev)
{
	struct nouveau_device *ndev = nouveau_device(dev);
	u32 fifo_data = nv_rd32(dev, NV04_PFB_FIFO_DATA);
	u32 cfg0 = nv_rd32(dev, 0x100200);

	ndev->vram_size = fifo_data & NV10_PFB_FIFO_DATA_RAM_AMOUNT_MB_MASK;

	if (cfg0 & 0x00000001)
		ndev->vram_type = NV_MEM_TYPE_DDR1;
	else
		ndev->vram_type = NV_MEM_TYPE_SDRAM;

	return 0;
}

int
nv10_fb_init(struct drm_device *dev)
{
	struct nouveau_device *ndev = nouveau_device(dev);
	struct nouveau_fb_engine *pfb = &ndev->subsys.fb;
	int i;

	/* Turn all the tiling regions off. */
	pfb->num_tiles = NV10_PFB_TILE__SIZE;
	for (i = 0; i < pfb->num_tiles; i++)
		pfb->set_tile_region(dev, i);

	return 0;
}

void
nv10_fb_takedown(struct drm_device *dev)
{
	struct nouveau_device *ndev = nouveau_device(dev);
	struct nouveau_fb_engine *pfb = &ndev->subsys.fb;
	int i;

	for (i = 0; i < pfb->num_tiles; i++)
		pfb->free_tile_region(dev, i);
}
