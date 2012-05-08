#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"

static struct drm_mm_node *
nv20_fb_alloc_tag(struct nouveau_device *ndev, u32 size)
{
	struct nouveau_fb_engine *pfb = &ndev->subsys.fb;
	struct drm_mm_node *mem;
	int ret;

	ret = drm_mm_pre_get(&pfb->tag_heap);
	if (ret)
		return NULL;

	spin_lock(&ndev->tile.lock);
	mem = drm_mm_search_free(&pfb->tag_heap, size, 0, 0);
	if (mem)
		mem = drm_mm_get_block_atomic(mem, size, 0);
	spin_unlock(&ndev->tile.lock);

	return mem;
}

static void
nv20_fb_free_tag(struct nouveau_device *ndev, struct drm_mm_node **pmem)
{
	struct drm_mm_node *mem = *pmem;
	if (mem) {
		spin_lock(&ndev->tile.lock);
		drm_mm_put_block(mem);
		spin_unlock(&ndev->tile.lock);
		*pmem = NULL;
	}
}

void
nv20_fb_init_tile_region(struct nouveau_device *ndev, int i, u32 addr,
			 u32 size, u32 pitch, u32 flags)
{
	struct nouveau_tile_reg *tile = &ndev->tile.reg[i];
	int bpp = (flags & NOUVEAU_GEM_TILE_32BPP ? 32 : 16);

	tile->addr  = 0x00000001 | addr;
	tile->limit = max(1u, addr + size) - 1;
	tile->pitch = pitch;

	/* Allocate some of the on-die tag memory, used to store Z
	 * compression meta-data (most likely just a bitmap determining
	 * if a given tile is compressed or not).
	 */
	if (flags & NOUVEAU_GEM_TILE_ZETA) {
		tile->tag_mem = nv20_fb_alloc_tag(ndev, size / 256);
		if (tile->tag_mem) {
			/* Enable Z compression */
			tile->zcomp = tile->tag_mem->start;
			if (ndev->chipset >= 0x25) {
				if (bpp == 16)
					tile->zcomp |= NV25_PFB_ZCOMP_MODE_16;
				else
					tile->zcomp |= NV25_PFB_ZCOMP_MODE_32;
			} else {
				tile->zcomp |= NV20_PFB_ZCOMP_EN;
				if (bpp != 16)
					tile->zcomp |= NV20_PFB_ZCOMP_MODE_32;
			}
		}

		tile->addr |= 2;
	}
}

void
nv20_fb_free_tile_region(struct nouveau_device *ndev, int i)
{
	struct nouveau_tile_reg *tile = &ndev->tile.reg[i];

	tile->addr = tile->limit = tile->pitch = tile->zcomp = 0;
	nv20_fb_free_tag(ndev, &tile->tag_mem);
}

void
nv20_fb_set_tile_region(struct nouveau_device *ndev, int i)
{
	struct nouveau_tile_reg *tile = &ndev->tile.reg[i];

	nv_wr32(ndev, NV10_PFB_TLIMIT(i), tile->limit);
	nv_wr32(ndev, NV10_PFB_TSIZE(i), tile->pitch);
	nv_wr32(ndev, NV10_PFB_TILE(i), tile->addr);
	nv_wr32(ndev, NV20_PFB_ZCOMP(i), tile->zcomp);
}

int
nv20_fb_vram_init(struct nouveau_device *ndev)
{
	u32 mem_size = nv_rd32(ndev, 0x10020c);
	u32 pbus1218 = nv_rd32(ndev, 0x001218);

	ndev->vram_size = mem_size & 0xff000000;
	switch (pbus1218 & 0x00000300) {
	case 0x00000000: ndev->vram_type = NV_MEM_TYPE_SDRAM; break;
	case 0x00000100: ndev->vram_type = NV_MEM_TYPE_DDR1; break;
	case 0x00000200: ndev->vram_type = NV_MEM_TYPE_GDDR3; break;
	case 0x00000300: ndev->vram_type = NV_MEM_TYPE_GDDR2; break;
	}

	return 0;
}

int
nv20_fb_init(struct nouveau_device *ndev)
{
	struct nouveau_fb_engine *pfb = &ndev->subsys.fb;
	int i;

	if (ndev->chipset >= 0x25)
		drm_mm_init(&pfb->tag_heap, 0, 64 * 1024);
	else
		drm_mm_init(&pfb->tag_heap, 0, 32 * 1024);

	/* Turn all the tiling regions off. */
	pfb->num_tiles = NV10_PFB_TILE__SIZE;
	for (i = 0; i < pfb->num_tiles; i++)
		pfb->set_tile_region(ndev, i);

	return 0;
}

void
nv20_fb_takedown(struct nouveau_device *ndev)
{
	struct nouveau_fb_engine *pfb = &ndev->subsys.fb;
	int i;

	for (i = 0; i < pfb->num_tiles; i++)
		pfb->free_tile_region(ndev, i);

	drm_mm_takedown(&pfb->tag_heap);
}
