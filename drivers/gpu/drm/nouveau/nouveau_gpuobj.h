#ifndef __NOUVEAU_GPUOBJ_H__
#define __NOUVEAU_GPUOBJ_H__

#define NVOBJ_FLAG_ZERO_ALLOC		(1 << 1)
#define NVOBJ_FLAG_ZERO_FREE		(1 << 2)
#define NVOBJ_FLAG_VM			(1 << 3)
#define NVOBJ_FLAG_VM_USER		(1 << 4)

#define NVOBJ_CINST_GLOBAL	0xdeadbeef

struct nouveau_gpuobj {
	struct nouveau_device *device;
	struct kref refcount;
	struct list_head list;

	void *node;
	u32 *suspend;

	u32 flags;

	u32 size;
	u32 pinst;	/* PRAMIN BAR offset */
	u32 cinst;	/* Channel offset */
	u64 vinst;	/* VRAM address */
	u64 linst;	/* VM address */

	u32 engine;
	u32 class;

	void (*dtor)(struct nouveau_device *, struct nouveau_gpuobj *);
	void *priv;
};

int  nouveau_gpuobj_create(struct nouveau_device *, int subdev);

int  nouveau_gpuobj_class_new(struct nouveau_device *, u32 class, u32 eng);
int  nouveau_gpuobj_mthd_new(struct nouveau_device *, u32 class, u32 mthd,
				    int (*exec)(struct nouveau_channel *,
						u32 class, u32 mthd, u32 data));
int  nouveau_gpuobj_mthd_call(struct nouveau_channel *, u32, u32, u32);
int  nouveau_gpuobj_mthd_call2(struct nouveau_device *, int, u32, u32, u32);
int  nouveau_gpuobj_channel_init(struct nouveau_channel *, u32 vram, u32 gart);
void nouveau_gpuobj_channel_takedown(struct nouveau_channel *);

int  nouveau_gpuobj_new(struct nouveau_device *, struct nouveau_channel *,
			u32 size, int align, u32 flags,
			struct nouveau_gpuobj **);
void nouveau_gpuobj_ref(struct nouveau_gpuobj *,
			       struct nouveau_gpuobj **);
int  nouveau_gpuobj_new_fake(struct nouveau_device *, u32 pinst, u64 vinst,
			     u32 size, u32 flags,
			     struct nouveau_gpuobj **);

int  nouveau_gpuobj_dma_new(struct nouveau_channel *, int class,
			    u64 offset, u64 size, int access, int target,
			    struct nouveau_gpuobj **);
int  nouveau_gpuobj_gr_new(struct nouveau_channel *, u32 handle, int class);
int  nv50_gpuobj_dma_new(struct nouveau_channel *, int class, u64 base,
			 u64 size, int target, int access, u32 type, u32 comp,
			 struct nouveau_gpuobj **pobj);
void nv50_gpuobj_dma_init(struct nouveau_gpuobj *, u32 offset, int class,
			  u64 base, u64 size, int target, int access,
			  u32 type, u32 comp);

#define NVDEV_ENGINE_ADD(ndev, e, p) do {                                      \
	(ndev)->engine[NVDEV_ENGINE_##e] = (p);                                \
} while (0)

#define NVDEV_ENGINE_DEL(ndev, e) do {                                         \
	(ndev)->engine[NVDEV_ENGINE_##e] = NULL;                               \
} while (0)

#define NVOBJ_CLASS(d, c, e) do {                                              \
	int ret = nouveau_gpuobj_class_new((d), (c), NVDEV_ENGINE_##e);        \
	if (ret)                                                               \
		return ret;                                                    \
} while (0)

#define NVOBJ_MTHD(d, c, m, e) do {                                            \
	int ret = nouveau_gpuobj_mthd_new((d), (c), (m), (e));                 \
	if (ret)                                                               \
		return ret;                                                    \
} while (0)


#endif
