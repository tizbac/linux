#ifndef __NOUVEAU_FB_H__
#define __NOUVEAU_FB_H__

struct nouveau_fb {
	struct nouveau_subdev base;

	bool (*memtype_valid)(struct nouveau_fb *, u32 memtype);
	int  (*vram_get)(struct nouveau_fb *, u64 size, u32 align, u32 size_nc,
			 u32 type, struct nouveau_mem **);
	void (*vram_put)(struct nouveau_fb *, struct nouveau_mem **);
	struct nouveau_mm mm;

	int num_tiles;
	void (*init_tile_region)(struct nouveau_fb *, int i, u32 addr,
				 u32 size, u32 pitch, u32 flags);
	void (*set_tile_region)(struct nouveau_fb *, int i);
	void (*free_tile_region)(struct nouveau_fb *, int i);

	struct drm_mm tag_heap;
};

int  nv04_fb_create(struct nouveau_device *, int);
bool nv04_fb_memtype_valid(struct nouveau_fb *, u32 memtype);

int  nv10_fb_create(struct nouveau_device *, int);
void nv10_fb_destroy(struct nouveau_device *, int);
int  nv10_fb_init(struct nouveau_device *, int);
void nv10_fb_set_tile_region(struct nouveau_fb *, int i);

int  nv20_fb_create(struct nouveau_device *, int);

int  nv30_fb_create(struct nouveau_device *, int);
void nv30_fb_init_tile_region(struct nouveau_fb *, int i, u32 addr,
			      u32 size, u32 pitch, u32 flags);
void nv30_fb_free_tile_region(struct nouveau_fb *, int i);

int  nv40_fb_create(struct nouveau_device *, int);

int  nv50_fb_create(struct nouveau_device *, int);
void nv50_fb_vram_fini(struct nouveau_fb *);
void nv50_fb_vram_del(struct nouveau_fb *, struct nouveau_mem **);
void nv50_fb_vm_trap(struct nouveau_device *, int display);

int  nvc0_fb_create(struct nouveau_device *, int);

#endif
