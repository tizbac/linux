#ifndef __NOUVEAU_FB_H__
#define __NOUVEAU_FB_H__

struct nouveau_fb {
	struct nouveau_subdev base;

	bool (*memtype_valid)(struct nouveau_fb *, u32 memtype);

	struct {
		enum {
			NV_MEM_TYPE_UNKNOWN = 0,
			NV_MEM_TYPE_STOLEN,
			NV_MEM_TYPE_SGRAM,
			NV_MEM_TYPE_SDRAM,
			NV_MEM_TYPE_DDR1,
			NV_MEM_TYPE_DDR2,
			NV_MEM_TYPE_DDR3,
			NV_MEM_TYPE_GDDR2,
			NV_MEM_TYPE_GDDR3,
			NV_MEM_TYPE_GDDR4,
			NV_MEM_TYPE_GDDR5
		} type;
		u64 stolen;
		u64 size;
		int ranks;

		struct nouveau_mm mm;

		int  (*get)(struct nouveau_fb *, u64 size, u32 align,
			    u32 size_nc, u32 type, struct nouveau_mem **);
		void (*put)(struct nouveau_fb *, struct nouveau_mem **);
	} ram;

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
void nv50_fb_vram_del(struct nouveau_fb *, struct nouveau_mem **);
void nv50_fb_vm_trap(struct nouveau_device *, int display);

int  nvc0_fb_create(struct nouveau_device *, int);

#endif
